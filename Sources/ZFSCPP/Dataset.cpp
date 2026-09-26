#include "internal/ZFSInternal.hpp"

#include <libnvpair.h>
#include <libzfs_core.h>
#include <zfs_prop.h>

#include <cerrno>
#include <cstring>
#include <exception>
#include <functional>
#include <thread>
#include <utility>

#include <unistd.h>

namespace zfs {

	Dataset::Impl::Impl(std::shared_ptr<detail::Context> context, std::string name,
			zfs_type_t type)
		: context_(std::move(context)), name_(std::move(name)), type_(type)
	{
		if (context_ == nullptr || name_.empty()) {
			throw Error(Error::Code::invalid_argument, 0,
					"invalid dataset implementation");
		}
	}

	const std::string&
		Dataset::Impl::name() const noexcept
		{
			return name_;
		}

	const std::shared_ptr<detail::Context>&
		Dataset::Impl::context() const noexcept
		{
			return context_;
		}

	zfs_type_t
		Dataset::Impl::type() const noexcept
		{
			return type_;
		}

	void
		Dataset::Impl::add_child(const std::shared_ptr<Impl>& child)
		{
			children_.push_back(child);
		}

	const std::vector<std::shared_ptr<Dataset::Impl>>&
		Dataset::Impl::children() const noexcept
		{
			return children_;
		}

	/*
	 * Dataset discovery stores only the dataset name and type.  A full libzfs
	 * handle is opened lazily for operations that actually need properties or
	 * mount state.  This avoids the substantial cost of fully populating every
	 * handle during bulk enumeration.
	 */
	zfs_handle_t*
		Dataset::Impl::open() const
		{
			zfs_handle_t* handle = zfs_open(context_->handle(), name_.c_str(), type_);
			if (handle == nullptr)
				detail::throw_libzfs_error(*context_, name_.c_str());
			return handle;
		}

	namespace {

		Error::Code
			error_code_from_errno(int error) noexcept
			{
				switch (error) {
					case EACCES:
					case EPERM:
						return Error::Code::permission_denied;
					case ENOENT:
						return Error::Code::not_found;
					case EEXIST:
						return Error::Code::already_exists;
					case EIO:
					case EPIPE:
						return Error::Code::io_error;
					case ENOMEM:
						return Error::Code::no_memory;
					case ENOTSUP:
						return Error::Code::unsupported;
					case EINVAL:
					case EBADF:
					case EXDEV:
						return Error::Code::invalid_argument;
					default:
						return Error::Code::unknown;
				}
			}

		[[noreturn]] void
			throw_lzc_error(int error, const char* operation)
			{
				std::string message(operation != nullptr ? operation : "libzfs_core operation");
				if (error != 0) {
					message += ": ";
					message += std::strerror(error);
				}
				throw Error(error_code_from_errno(error), error, message);
			}

		enum lzc_send_flags
			send_flags(const SendOptions& options) noexcept
			{
				unsigned int flags = 0;
				if (options.embedded_data)
					flags |= LZC_SEND_FLAG_EMBED_DATA;
				if (options.large_blocks)
					flags |= LZC_SEND_FLAG_LARGE_BLOCK;
				if (options.compressed)
					flags |= LZC_SEND_FLAG_COMPRESS;
				if (options.raw)
					flags |= LZC_SEND_FLAG_RAW;
				return static_cast<enum lzc_send_flags>(flags);
			}

		PropertySource
			property_source(zprop_source_t source) noexcept
			{
				switch (source) {
					case ZPROP_SRC_NONE: return PropertySource::none;
					case ZPROP_SRC_DEFAULT: return PropertySource::default_value;
					case ZPROP_SRC_TEMPORARY: return PropertySource::temporary;
					case ZPROP_SRC_LOCAL: return PropertySource::local;
					case ZPROP_SRC_INHERITED: return PropertySource::inherited;
					case ZPROP_SRC_RECEIVED: return PropertySource::received;
					default: return PropertySource::unknown;
				}
			}

		PropertyType
			property_type(zfs_prop_t prop) noexcept
			{
				switch (zfs_prop_get_type(prop)) {
					case PROP_TYPE_NUMBER: return PropertyType::number;
					case PROP_TYPE_STRING: return PropertyType::string;
					case PROP_TYPE_INDEX: return PropertyType::index;
					default: return PropertyType::unknown;
				}
			}

		PropertySource
			user_property_source(const char* source, const char* dataset) noexcept
			{
				if (source == nullptr || source[0] == '\0')
					return PropertySource::none;
				if (std::strcmp(source, ZPROP_SOURCE_VAL_RECVD) == 0)
					return PropertySource::received;
				if (dataset != nullptr && std::strcmp(source, dataset) == 0)
					return PropertySource::local;
				return PropertySource::inherited;
			}

		/* RAII owner for a temporary property nvlist. */
		class PropertyList {
			public:
				PropertyList()
				{
					if (nvlist_alloc(&list_, NV_UNIQUE_NAME, 0) != 0)
						throw Error(Error::Code::no_memory, 0,
								"nvlist_alloc() failed");
				}

				~PropertyList()
				{
					if (list_ != nullptr)
						nvlist_free(list_);
				}

				PropertyList(const PropertyList&) = delete;
				PropertyList& operator=(const PropertyList&) = delete;

				void add(const std::string& name, const std::string& value)
				{
					if (nvlist_add_string(list_, name.c_str(), value.c_str()) != 0)
						throw Error(Error::Code::no_memory, 0,
								"nvlist_add_string() failed");
				}

				nvlist_t* get() const noexcept { return list_; }

			private:
				nvlist_t* list_ = nullptr;
		};

		/* RAII owner for a lazily opened zfs_handle_t. */
		class DatasetHandle {
			public:
				explicit DatasetHandle(zfs_handle_t* handle) noexcept
					: handle_(handle)
					{
					}

				~DatasetHandle()
				{
					if (handle_ != nullptr)
						zfs_close(handle_);
				}

				DatasetHandle(const DatasetHandle&) = delete;
				DatasetHandle& operator=(const DatasetHandle&) = delete;

				zfs_handle_t* get() const noexcept { return handle_; }

			private:
				zfs_handle_t* handle_ = nullptr;
		};

	} // namespace


	class SendStream::Iterator::State {
		public:
			State(std::string target, std::string from, const SendOptions& options,
					std::shared_ptr<void> target_lifetime,
					std::shared_ptr<void> from_lifetime)
				: target_(std::move(target)), from_(std::move(from)),
				options_(options), target_lifetime_(std::move(target_lifetime)),
				from_lifetime_(std::move(from_lifetime))
		{
			int fds[2];
			if (::pipe(fds) != 0) {
				const int error = errno;
				throw Error(error_code_from_errno(error), error,
						std::string("pipe() for ZFS send failed: ") +
						std::strerror(error));
			}

			read_fd_ = fds[0];
			write_fd_ = fds[1];

			try {
				worker_ = std::thread([this] {
						const char* from = from_.empty() ? nullptr : from_.c_str();
						send_error_ = lzc_send(target_.c_str(), from, write_fd_,
								send_flags(options_));
						::close(write_fd_);
						write_fd_ = -1;
						});
			} catch (...) {
				::close(read_fd_);
				::close(write_fd_);
				read_fd_ = -1;
				write_fd_ = -1;
				throw;
			}
		}

			~State()
			{
				drain_and_finish_noexcept();
			}

			State(const State&) = delete;
			State& operator=(const State&) = delete;

			std::size_t read(void* buffer, std::size_t capacity)
			{
				if (capacity == 0)
					return 0;
				if (buffer == nullptr)
					throw Error(Error::Code::invalid_argument, EINVAL,
							"send stream read buffer is null");
				if (read_fd_ < 0)
					return finish();

				ssize_t count;
				do {
					count = ::read(read_fd_, buffer, capacity);
				} while (count < 0 && errno == EINTR);

				if (count > 0)
					return static_cast<std::size_t>(count);

				if (count < 0) {
					const int error = errno;
					throw Error(error_code_from_errno(error), error,
							std::string("read() from ZFS send stream failed: ") +
							std::strerror(error));
				}

				::close(read_fd_);
				read_fd_ = -1;
				return finish();
			}

		private:
			std::size_t finish()
			{
				if (worker_.joinable())
					worker_.join();
				if (send_error_ != 0)
					throw_lzc_error(send_error_, from_.empty()
							? "lzc_send()"
							: "lzc_send() incremental");
				return 0;
			}

			void drain_and_finish_noexcept() noexcept
			{
				if (read_fd_ >= 0) {
					unsigned char buffer[64 * 1024];
					for (;;) {
						ssize_t count;
						do {
							count = ::read(read_fd_, buffer, sizeof(buffer));
						} while (count < 0 && errno == EINTR);
						if (count <= 0)
							break;
					}
					::close(read_fd_);
					read_fd_ = -1;
				}
				if (worker_.joinable())
					worker_.join();
			}

			std::string target_;
			std::string from_;
			SendOptions options_;
			std::shared_ptr<void> target_lifetime_;
			std::shared_ptr<void> from_lifetime_;
			int read_fd_ = -1;
			int write_fd_ = -1;
			int send_error_ = 0;
			std::thread worker_;
	};

	SendStream::SendStream(std::string target, std::string from,
			const SendOptions& options, std::shared_ptr<void> target_lifetime,
			std::shared_ptr<void> from_lifetime)
		: state_(std::make_shared<State>(std::move(target), std::move(from),
					options, std::move(target_lifetime), std::move(from_lifetime)))
	{
	}

	std::size_t
		SendStream::read(void* buffer, std::size_t capacity)
		{
			if (state_ == nullptr)
				return 0;
			return state_->read(buffer, capacity);
		}

	SendStream::Iterator
		SendStream::begin()
		{
			if (state_ == nullptr)
				return Iterator();
			return Iterator(state_);
		}

	SendStream::Iterator
		SendStream::end() noexcept
		{
			return Iterator();
		}

	SendStream::Iterator::Iterator(std::shared_ptr<State> state)
		: state_(std::move(state)), done_(false)
	{
		read_next();
	}

	SendStream::Iterator::reference
		SendStream::Iterator::operator*() const noexcept
		{
			return chunk_;
		}

	SendStream::Iterator::pointer
		SendStream::Iterator::operator->() const noexcept
		{
			return &chunk_;
		}

	SendStream::Iterator&
		SendStream::Iterator::operator++()
		{
			read_next();
			return *this;
		}

	SendStream::Iterator
		SendStream::Iterator::operator++(int)
		{
			Iterator copy = *this;
			read_next();
			return copy;
		}

	void
		SendStream::Iterator::read_next()
		{
			if (done_ || state_ == nullptr)
				return;

			chunk_.resize(128 * 1024);
			const std::size_t count = state_->read(chunk_.data(), chunk_.size());
			if (count == 0) {
				chunk_.clear();
				done_ = true;
				state_.reset();
				return;
			}
			chunk_.resize(count);
		}

	bool
		operator==(const SendStream::Iterator& lhs,
				const SendStream::Iterator& rhs) noexcept
		{
			if (lhs.done_ && rhs.done_)
				return true;
			return lhs.done_ == rhs.done_ && lhs.state_ == rhs.state_;
		}

	bool
		operator!=(const SendStream::Iterator& lhs,
				const SendStream::Iterator& rhs) noexcept
		{
			return !(lhs == rhs);
		}

	Dataset::Dataset(std::shared_ptr<Impl> impl) noexcept
		: impl_(std::move(impl))
		{
		}

	Dataset::Dataset(Dataset&&) noexcept = default;
	Dataset& Dataset::operator=(Dataset&&) noexcept = default;
	Dataset::~Dataset() = default;

	bool
		Dataset::is_filesystem() const noexcept
		{
			return false;
		}

	bool
		Dataset::is_volume() const noexcept
		{
			return false;
		}

	bool
		Dataset::is_snapshot() const noexcept
		{
			return false;
		}

	std::string
		Dataset::name() const
		{
			return impl_->name();
		}

	std::uint64_t
		Dataset::guid() const
		{
			DatasetHandle handle(impl_->open());
			return static_cast<std::uint64_t>(
					zfs_prop_get_int(handle.get(), ZFS_PROP_GUID));
		}

	std::vector<Property>
		Dataset::properties() const
		{
			DatasetHandle dataset(impl_->open());
			zfs_handle_t* handle = dataset.get();
			const zfs_type_t dataset_type = zfs_get_type(handle);
			std::vector<Property> result;
			char value[ZFS_MAXPROPLEN];
			char source_location[ZFS_MAX_DATASET_NAME_LEN];

			for (int i = 0; i < ZFS_NUM_PROPS; ++i) {
				const auto prop = static_cast<zfs_prop_t>(i);
				if (zfs_prop_visible(prop) == B_FALSE ||
						zfs_prop_valid_for_type(prop, dataset_type, B_FALSE) == B_FALSE)
					continue;

				zprop_source_t source = ZPROP_SRC_NONE;
				if (zfs_prop_get(handle, prop, value, sizeof(value), &source,
							source_location, sizeof(source_location), B_FALSE) != 0) {
					detail::throw_libzfs_error(*impl_->context(), zfs_prop_to_name(prop));
				}

				result.push_back(Property(zfs_prop_to_name(prop), value,
							property_source(source), property_type(prop),
							zfs_prop_readonly(prop) != B_FALSE));
			}

			nvlist_t* user_props = zfs_get_user_props(handle);
			for (nvpair_t* pair = user_props != nullptr
					? nvlist_next_nvpair(user_props, nullptr)
					: nullptr;
					pair != nullptr; pair = nvlist_next_nvpair(user_props, pair)) {
				nvlist_t* prop_value = nullptr;
				const char* value_string = nullptr;
				const char* source_string = nullptr;
				if (nvpair_value_nvlist(pair, &prop_value) != 0 || prop_value == nullptr ||
						nvlist_lookup_string(prop_value, ZPROP_VALUE, &value_string) != 0)
					continue;

				(void) nvlist_lookup_string(prop_value, ZPROP_SOURCE, &source_string);
				result.push_back(Property(nvpair_name(pair), value_string,
							user_property_source(source_string, zfs_get_name(handle)),
							PropertyType::string, false));
			}

			return result;
		}

	Property
		Dataset::property(const std::string& name) const
		{
			DatasetHandle dataset(impl_->open());
			zfs_handle_t* handle = dataset.get();
			const zfs_prop_t prop = zfs_name_to_prop(name.c_str());

			if (prop != ZPROP_INVAL && zfs_prop_visible(prop) != B_FALSE &&
					zfs_prop_valid_for_type(prop, zfs_get_type(handle), B_FALSE) != B_FALSE) {
				char value[ZFS_MAXPROPLEN];
				char source_location[ZFS_MAX_DATASET_NAME_LEN];
				zprop_source_t source = ZPROP_SRC_NONE;
				if (zfs_prop_get(handle, prop, value, sizeof(value), &source,
							source_location, sizeof(source_location), B_FALSE) != 0) {
					detail::throw_libzfs_error(*impl_->context(), name.c_str());
				}
				return Property(name, value, property_source(source),
						property_type(prop), zfs_prop_readonly(prop) != B_FALSE);
			}

			if (zfs_prop_user(name.c_str()) != B_FALSE) {
				nvlist_t* user_props = zfs_get_user_props(handle);
				nvlist_t* prop_value = nullptr;
				const char* value_string = nullptr;
				const char* source_string = nullptr;
				if (user_props != nullptr &&
						nvlist_lookup_nvlist(user_props, name.c_str(), &prop_value) == 0 &&
						nvlist_lookup_string(prop_value, ZPROP_VALUE, &value_string) == 0) {
					(void) nvlist_lookup_string(prop_value, ZPROP_SOURCE, &source_string);
					return Property(name, value_string,
							user_property_source(source_string, zfs_get_name(handle)),
							PropertyType::string, false);
				}
			}

			throw Error(Error::Code::not_found, 0,
					"dataset property not found: " + name);
		}

	std::vector<Snapshot>
		Dataset::snapshots() const
		{
			DatasetHandle dataset(impl_->open());
			std::vector<Snapshot> result;
			std::exception_ptr exception;

			struct IteratorState {
				std::shared_ptr<detail::Context> context;
				std::vector<Snapshot>* snapshots;
				std::exception_ptr* exception;
			} state { impl_->context(), &result, &exception };

			auto callback = [](zfs_handle_t* handle, void* arg) -> int {
				auto* state = static_cast<IteratorState*>(arg);

				try {
					const char* snapshot_name = zfs_get_name(handle);
					if (snapshot_name != nullptr) {
						auto snapshot_impl = std::make_shared<Dataset::Impl>(
							state->context, snapshot_name, ZFS_TYPE_SNAPSHOT);
						state->snapshots->push_back(Snapshot(snapshot_impl));
					}
					zfs_close(handle);
					return 0;
				} catch (...) {
					zfs_close(handle);
					*state->exception = std::current_exception();
					return 1;
				}
			};

			/*
			 * Snapshot discovery needs only names/types, so use the lightweight v2
			 * iterator mode and close each iterator-owned handle immediately.
			 */
			const int error = zfs_iter_snapshots_v2(dataset.get(), ZFS_ITER_SIMPLE,
					callback, &state, 0, 0);
			if (exception != nullptr)
				std::rethrow_exception(exception);
			if (error != 0)
				detail::throw_libzfs_error(*impl_->context(),
						"zfs_iter_snapshots_v2()");

			return result;
		}

	Snapshot
		Dataset::create_snapshot(const std::string& snapshot_name,
				const PropertyValues& properties) const
		{
			if (is_snapshot()) {
				throw Error(Error::Code::invalid_argument, 0,
						"cannot create a snapshot of a snapshot");
			}
			if (snapshot_name.empty()) {
				throw Error(Error::Code::invalid_argument, 0,
						"snapshot name must not be empty");
			}

			std::unique_ptr<PropertyList> property_list;
			if (!properties.empty()) {
				property_list = std::make_unique<PropertyList>();
				for (const auto& [name, value] : properties) {
					if (zfs_prop_user(name.c_str()) == B_FALSE) {
						throw Error(Error::Code::invalid_argument, 0,
								"snapshot property is not a user property: " + name);
					}
					property_list->add(name, value);
				}
			}

			const std::string full_name = name() + "@" + snapshot_name;
			nvlist_t* props = property_list != nullptr ? property_list->get() : nullptr;
			if (zfs_snapshot(impl_->context()->handle(), full_name.c_str(), B_FALSE,
						props) != 0) {
				detail::throw_libzfs_error(*impl_->context(), "zfs_snapshot()");
			}

			auto snapshot_impl = std::make_shared<Dataset::Impl>(
					impl_->context(), full_name, ZFS_TYPE_SNAPSHOT);
			return Snapshot(std::move(snapshot_impl));
		}

	Filesystem::Filesystem(std::shared_ptr<Impl> impl) noexcept
		: Dataset(std::move(impl))
		{
		}

	bool
		Filesystem::is_filesystem() const noexcept
		{
			return true;
		}

	bool
		Filesystem::mounted() const
		{
			DatasetHandle handle(impl_->open());
			return zfs_is_mounted(handle.get(), nullptr) != B_FALSE;
		}

	std::string
		Filesystem::mountpoint() const
		{
			DatasetHandle handle(impl_->open());
			char value[ZFS_MAXPROPLEN];
			zprop_source_t source = ZPROP_SRC_NONE;
			char source_location[ZFS_MAXPROPLEN];

			if (zfs_prop_get(handle.get(), ZFS_PROP_MOUNTPOINT, value,
						sizeof(value), &source, source_location, sizeof(source_location),
						B_FALSE) != 0) {
				detail::throw_libzfs_error(*impl_->context(), "mountpoint");
			}

			return value;
		}

	Volume::Volume(std::shared_ptr<Impl> impl) noexcept
		: Dataset(std::move(impl))
		{
		}

	bool
		Volume::is_volume() const noexcept
		{
			return true;
		}

	std::uint64_t
		Volume::size() const
		{
			DatasetHandle handle(impl_->open());
			return static_cast<std::uint64_t>(
					zfs_prop_get_int(handle.get(), ZFS_PROP_VOLSIZE));
		}

	std::uint64_t
		Volume::block_size() const
		{
			DatasetHandle handle(impl_->open());
			return static_cast<std::uint64_t>(
					zfs_prop_get_int(handle.get(), ZFS_PROP_VOLBLOCKSIZE));
		}

	Snapshot::Snapshot(std::shared_ptr<Impl> impl) noexcept
		: Dataset(std::move(impl))
		{
		}

	bool
		Snapshot::is_snapshot() const noexcept
		{
			return true;
		}

	std::vector<Snapshot>
		Snapshot::snapshots() const
		{
			return {};
		}

	Snapshot
		Snapshot::create_snapshot(const std::string&, const PropertyValues&) const
		{
			throw Error(Error::Code::invalid_argument, 0,
					"cannot create a snapshot of a snapshot");
		}

	SendStream
		Snapshot::send(const SendOptions& options) const
		{
			return SendStream(name(), std::string(), options, impl_);
		}

	SendStream
		Snapshot::send(const Snapshot& from, const SendOptions& options) const
		{
			return SendStream(name(), from.name(), options, impl_, from.impl_);
		}

	void
		Snapshot::send_to(int fd, const SendOptions& options) const
		{
			if (fd < 0)
				throw Error(Error::Code::invalid_argument, EBADF,
						"send output file descriptor is invalid");

			const std::string target_name = name();
			const int error = lzc_send(target_name.c_str(), nullptr, fd,
					send_flags(options));
			if (error != 0)
				throw_lzc_error(error, "lzc_send()");
		}

	void
		Snapshot::send_to(int fd, const Snapshot& from,
				const SendOptions& options) const
		{
			if (fd < 0)
				throw Error(Error::Code::invalid_argument, EBADF,
						"send output file descriptor is invalid");

			const std::string target_name = name();
			const std::string from_name = from.name();
			const int error = lzc_send(target_name.c_str(), from_name.c_str(), fd,
					send_flags(options));
			if (error != 0)
				throw_lzc_error(error, "lzc_send() incremental");
		}

	const std::vector<Filesystem>&
		DatasetCollection::filesystems() const & noexcept
		{
			return filesystems_;
		}

	std::vector<Filesystem>
		DatasetCollection::filesystems() && noexcept
		{
			return std::move(filesystems_);
		}

	const std::vector<Volume>&
		DatasetCollection::volumes() const & noexcept
		{
			return volumes_;
		}

	std::vector<Volume>
		DatasetCollection::volumes() && noexcept
		{
			return std::move(volumes_);
		}

	DatasetCollection
		Pool::datasets() const
		{
			DatasetCollection result;
			auto context = impl_->context();
			const std::string pool_name = name();

			zfs_handle_t* root = zfs_open(context->handle(), pool_name.c_str(),
					ZFS_TYPE_FILESYSTEM);
			if (root == nullptr)
				detail::throw_libzfs_error(*context, "zfs_open() root filesystem");

			try {
				/*
				 * Build the complete filesystem/volume hierarchy during the same
				 * lightweight traversal used for bulk dataset enumeration.  Each
				 * Dataset::Impl records its immediate children, so later children()
				 * calls are in-memory operations rather than additional libzfs walks.
				 */
				auto root_impl = std::make_shared<Dataset::Impl>(
						context, zfs_get_name(root), ZFS_TYPE_FILESYSTEM);
				result.filesystems_.push_back(Filesystem(root_impl));

				using Visit = std::function<int(
					zfs_handle_t*, const std::shared_ptr<Dataset::Impl>&)>;
				Visit visit;
				std::exception_ptr exception;

				struct IteratorState {
					Visit* visit;
					std::shared_ptr<Dataset::Impl> parent;
				} ;

				auto callback = [](zfs_handle_t* handle, void* arg) -> int {
					auto* state = static_cast<IteratorState*>(arg);
					return (*state->visit)(handle, state->parent);
				};

				visit = [&](zfs_handle_t* handle,
						const std::shared_ptr<Dataset::Impl>& parent) -> int {
					try {
						const zfs_type_t type = zfs_get_type(handle);
						const char* dataset_name = zfs_get_name(handle);
						if (dataset_name == nullptr) {
							zfs_close(handle);
							return 0;
						}

						if (type != ZFS_TYPE_FILESYSTEM && type != ZFS_TYPE_VOLUME) {
							zfs_close(handle);
							return 0;
						}

						auto dataset_impl = std::make_shared<Dataset::Impl>(
								context, dataset_name, type);
						parent->add_child(dataset_impl);

						if (type == ZFS_TYPE_FILESYSTEM) {
							result.filesystems_.push_back(Filesystem(dataset_impl));
							IteratorState child_state { &visit, dataset_impl };
							const int error = zfs_iter_filesystems_v2(handle,
									ZFS_ITER_SIMPLE, callback, &child_state);
							zfs_close(handle);
							return error;
						}

						result.volumes_.push_back(Volume(dataset_impl));
						zfs_close(handle);
						return 0;
					} catch (...) {
						zfs_close(handle);
						exception = std::current_exception();
						return 1;
					}
				};

				IteratorState root_state { &visit, root_impl };
				const int error = zfs_iter_filesystems_v2(root, ZFS_ITER_SIMPLE,
						callback, &root_state);
				zfs_close(root);
				root = nullptr;

				if (exception != nullptr)
					std::rethrow_exception(exception);
				if (error != 0)
					detail::throw_libzfs_error(*context, "zfs_iter_filesystems_v2()");
			} catch (...) {
				if (root != nullptr)
					zfs_close(root);
				throw;
			}

			return result;
		}

	std::vector<Filesystem>
		Pool::filesystems() const
		{
			auto result = datasets();
			return std::move(result.filesystems_);
		}

	std::vector<Volume>
		Pool::volumes() const
		{
			auto result = datasets();
			return std::move(result.volumes_);
		}

	DatasetCollection
		Dataset::children() const
		{
			DatasetCollection result;
			for (const auto& child : impl_->children()) {
				if (child->type() == ZFS_TYPE_FILESYSTEM)
					result.filesystems_.push_back(Filesystem(child));
				else if (child->type() == ZFS_TYPE_VOLUME)
					result.volumes_.push_back(Volume(child));
			}
			return result;
		}

	DatasetCollection
		Pool::children() const
		{
			auto all = datasets();
			if (all.filesystems_.empty())
				return {};
			return all.filesystems_.front().children();
		}

} // namespace zfs
