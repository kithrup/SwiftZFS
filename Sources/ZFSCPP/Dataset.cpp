#include "internal/ZFSInternal.hpp"

#include <libnvpair.h>
#include <libzfs_core.h>
#include <zfs_prop.h>
#include <sys/zfs_ioctl.h>
#if __has_include(<sys/simd_config.h>)
#include <sys/simd_config.h>
#endif
#include <zfs_fletcher.h>

#include <cerrno>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
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

	namespace
	{
		using SendMetadata = std::unique_ptr<nvlist_t, decltype(&nvlist_free)>;

		void
		send_nv_check(int error)
		{
			if (error != 0)
				throw_lzc_error(error, "encode replication metadata");
		}

		SendMetadata
		make_send_metadata()
		{
			nvlist_t* list = nullptr;
			send_nv_check(nvlist_alloc(&list, NV_UNIQUE_NAME, 0));
			return SendMetadata(list, &nvlist_free);
		}

		[[noreturn]] void
		invalid_selection(const std::string& message)
		{
			throw Error(Error::Code::invalid_argument, EINVAL, "replication header: " + message);
		}

		std::string
		snapshot_dataset(const std::string& name)
		{
			return name.substr(0, name.find('@'));
		}

		std::string
		snapshot_component(const std::string& name)
		{
			return name.substr(name.find('@') + 1);
		}

		void
		capture_send_properties(zfs_handle_t* handle, nvlist_t* result)
		{
			// Keep values in their native nvlist types, including index properties.
			// Only local and received properties belong in replication metadata.
			for (nvlist_t* props : {zfs_get_all_props(handle), zfs_get_user_props(handle)}) {
				if (props == nullptr)
					continue;
				for (nvpair_t* pair = nvlist_next_nvpair(props, nullptr); pair != nullptr;
				     pair = nvlist_next_nvpair(props, pair)) {
					const char* name = nvpair_name(pair);
					const zfs_prop_t prop = zfs_name_to_prop(name);
					const bool user = zfs_prop_user(name) != B_FALSE;
					if (!user && (prop == ZPROP_INVAL || zfs_prop_readonly(prop)))
						continue;
					const bool space_limit =
					    prop == ZFS_PROP_QUOTA || prop == ZFS_PROP_RESERVATION ||
					    prop == ZFS_PROP_REFQUOTA || prop == ZFS_PROP_REFRESERVATION;
					if (space_limit && zfs_get_type(handle) == ZFS_TYPE_SNAPSHOT)
						continue;
					nvlist_t* value = nullptr;
					send_nv_check(nvpair_value_nvlist(pair, &value));
					const char* source = nullptr;
					if (nvlist_lookup_string(value, ZPROP_SOURCE, &source) == 0) {
						if (std::strcmp(source, zfs_get_name(handle)) != 0 &&
						    std::strcmp(source, ZPROP_SOURCE_VAL_RECVD) != 0)
							continue;
					} else if (!space_limit) {
						continue;
					}
					if (user || zfs_prop_get_type(prop) == PROP_TYPE_STRING) {
						const char* text = nullptr;
						send_nv_check(
						    nvlist_lookup_string(value, ZPROP_VALUE, &text));
						send_nv_check(nvlist_add_string(result, name, text));
					} else {
						uint64_t number = 0;
						send_nv_check(
						    nvlist_lookup_uint64(value, ZPROP_VALUE, &number));
						send_nv_check(nvlist_add_uint64(result, name, number));
					}
				}
			}
		}

		SendStream::Chunk
		encode_compound_header(const std::string& target, void* payload = nullptr,
				       std::size_t payload_size = 0)
		{
			dmu_replay_record_t begin_record;
			std::memset(&begin_record, 0, sizeof(begin_record));
			auto& begin = begin_record.drr_u.drr_begin;
			if (target.size() >= sizeof(begin.drr_toname) ||
			    target.find('\0') != std::string::npos ||
			    !zfs_name_valid(target.c_str(), ZFS_TYPE_SNAPSHOT))
				invalid_selection("target must be a valid full snapshot name");
			if (payload_size > std::numeric_limits<uint32_t>::max())
				invalid_selection("metadata exceeds the stream record size limit");
			begin_record.drr_type = dmu_replay_record_t::DRR_BEGIN;
			begin_record.drr_payloadlen = static_cast<uint32_t>(payload_size);
			begin.drr_magic = DMU_BACKUP_MAGIC;
			DMU_SET_STREAM_HDRTYPE(begin.drr_versioninfo, DMU_COMPOUNDSTREAM);
			std::memcpy(begin.drr_toname, target.c_str(), target.size() + 1);

			zio_cksum_t checksum = {};
			fletcher_4_incremental_native(&begin_record, sizeof(begin_record), &checksum);
			if (payload_size != 0)
				fletcher_4_incremental_native(payload, payload_size, &checksum);
			dmu_replay_record_t end;
			std::memset(&end, 0, sizeof(end));
			end.drr_type = dmu_replay_record_t::DRR_END;
			end.drr_u.drr_end.drr_checksum = checksum;
			SendStream::Chunk bytes(sizeof(begin_record) + payload_size + sizeof(end));
			std::memcpy(bytes.data(), &begin_record, sizeof(begin_record));
			if (payload_size != 0)
				std::memcpy(bytes.data() + sizeof(begin_record), payload, payload_size);
			std::memcpy(bytes.data() + sizeof(begin_record) + payload_size, &end, sizeof(end));
			return bytes;
		}
	} // namespace

	SendStream::Chunk
	SendStream::compound_header(const std::string& target)
	{
		return encode_compound_header(target);
	}

	SendStream::Chunk
	SendStream::compound_footer()
	{
		dmu_replay_record_t end;
		std::memset(&end, 0, sizeof(end));
		end.drr_type = dmu_replay_record_t::DRR_END;
		Chunk bytes(sizeof(end));
		std::memcpy(bytes.data(), &end, sizeof(end));
		return bytes;
	}

	SendStream::Chunk
	SendStream::replication_header(const Snapshot& target,
				       const std::vector<ReplicationDataset>& datasets,
				       const SendOptions& options)
	{
		const std::string root = snapshot_dataset(target.path());
		DatasetHandle target_handle(target.impl_->open());
		const uint64_t target_txg = zfs_prop_get_int(target_handle.get(), ZFS_PROP_CREATETXG);
		const uint64_t target_guid = zfs_prop_get_int(target_handle.get(), ZFS_PROP_GUID);
		struct Entry {
			SendMetadata metadata;
			const ReplicationDataset* selection;
			uint64_t anchor;
			uint64_t guid;
			std::string origin;
		};
		std::map<std::string, Entry> entries;
		std::set<uint64_t> dataset_guids;
		for (const auto& selection : datasets) {
			if (selection.dataset == nullptr || selection.dataset->is_snapshot() ||
			    selection.snapshots.empty())
				invalid_selection("each dataset needs at least one target snapshot");
			const std::string name = selection.dataset->path();
			if (name != root && name.compare(0, root.size() + 1, root + "/") != 0)
				invalid_selection("dataset is outside the package root: " + name);
			if (entries.find(name) != entries.end())
				invalid_selection("duplicate dataset: " + name);
			DatasetHandle handle(selection.dataset->impl_->open());
			const uint64_t guid = zfs_prop_get_int(handle.get(), ZFS_PROP_GUID);
			if (!dataset_guids.insert(guid).second)
				invalid_selection("duplicate dataset GUID: " + name);
			auto metadata = make_send_metadata();
			send_nv_check(nvlist_add_string(metadata.get(), "name", name.c_str()));
			auto properties = make_send_metadata();
			capture_send_properties(handle.get(), properties.get());
			send_nv_check(nvlist_add_nvlist(metadata.get(), "props", properties.get()));
			if (zfs_prop_get_int(handle.get(), ZFS_PROP_ENCRYPTION) != ZIO_CRYPT_OFF) {
				if (!options.raw)
					invalid_selection("encrypted replication requires raw sends: " +
							  name);
				boolean_t is_root = B_FALSE;
				if (zfs_crypto_get_encryption_root(handle.get(), &is_root, nullptr) != 0)
					detail::throw_libzfs_error(*selection.dataset->impl_->context(),
								   "replication encryption root");
				if (is_root)
					send_nv_check(nvlist_add_boolean(metadata.get(), "is_encroot"));
			}
			char origin[ZFS_MAX_DATASET_NAME_LEN] = {};
			// Non-clones have no origin value; libzfs returns -1 without an error.
			if (zfs_prop_get(handle.get(), ZFS_PROP_ORIGIN, origin, sizeof(origin), nullptr,
					 nullptr, 0, B_TRUE) != 0)
				origin[0] = 0;
			const bool clone = origin[0] != '\0' && std::strcmp(origin, "-") != 0;
			if (clone) {
				DatasetHandle origin_handle(
				    zfs_open(selection.dataset->impl_->context()->handle(), origin,
					     ZFS_TYPE_SNAPSHOT));
				if (origin_handle.get() == nullptr)
					detail::throw_libzfs_error(*selection.dataset->impl_->context(),
								   "replication clone origin");
				send_nv_check(
				    nvlist_add_uint64(metadata.get(), "origin",
						      zfs_prop_get_int(origin_handle.get(), ZFS_PROP_GUID)));
				if (selection.from == nullptr)
					invalid_selection(
					    "a clone needs its origin or a local incremental base: " + name);
			}

			auto snapshots = make_send_metadata();
			auto snapshot_properties = make_send_metadata();
			uint64_t previous_txg = 0;
			uint64_t base_guid = 0;
			uint64_t last_guid = 0;
			auto capture_snapshot = [&](const Snapshot* snapshot, bool base,
						    bool retained = false) {
				if (snapshot == nullptr)
					invalid_selection("null snapshot for " + name);
				const bool local = snapshot_dataset(snapshot->path()) == name;
				if (!local && (!base || !clone || snapshot->path() != origin))
					invalid_selection("snapshot does not belong to dataset: " +
							  snapshot->path());
				DatasetHandle snapshot_handle(snapshot->impl_->open());
				const uint64_t txg =
				    zfs_prop_get_int(snapshot_handle.get(), ZFS_PROP_CREATETXG);
				const uint64_t snap_guid =
				    zfs_prop_get_int(snapshot_handle.get(), ZFS_PROP_GUID);
				if ((!retained && txg <= previous_txg) || txg > target_txg)
					invalid_selection("snapshots must be strictly chronological and no "
							  "later than target: " +
							  snapshot->path());
				if (!retained)
					previous_txg = txg;
				if (base && local)
					base_guid = snap_guid;
				if (local) {
					const std::string component = snapshot_component(snapshot->path());
					send_nv_check(
					    nvlist_add_uint64(snapshots.get(), component.c_str(), snap_guid));
					auto props = make_send_metadata();
					capture_send_properties(snapshot_handle.get(), props.get());
					send_nv_check(nvlist_add_nvlist(snapshot_properties.get(),
									component.c_str(), props.get()));
				}
				if (!retained)
					last_guid = snap_guid;
			};
			if (selection.from != nullptr)
				capture_snapshot(selection.from, true);
			for (const Snapshot* snapshot : selection.snapshots)
				capture_snapshot(snapshot, false);
			for (const Snapshot* snapshot : selection.retained_snapshots)
				capture_snapshot(snapshot, false, true);
			if (name == root && last_guid != target_guid)
				invalid_selection("root entry must end at the package target");
			send_nv_check(nvlist_add_nvlist(metadata.get(), "snaps", snapshots.get()));
			send_nv_check(
			    nvlist_add_nvlist(metadata.get(), "snapprops", snapshot_properties.get()));
			entries.emplace(name, Entry{std::move(metadata), &selection,
						    base_guid != 0 ? base_guid : last_guid, guid,
						    clone ? origin : ""});
		}
		auto root_entry = entries.find(root);
		if (root_entry == entries.end())
			invalid_selection("the package root must be selected");
		auto header = make_send_metadata();
		const std::string tosnap = snapshot_component(target.path());
		send_nv_check(nvlist_add_string(header.get(), "tosnap", tosnap.c_str()));
		if (root_entry->second.selection->from != nullptr &&
		    snapshot_dataset(root_entry->second.selection->from->path()) == root) {
			const auto& from = *root_entry->second.selection->from;
			const std::string fromsnap = snapshot_component(from.path());
			send_nv_check(nvlist_add_string(header.get(), "fromsnap", fromsnap.c_str()));
		}
		if (options.raw)
			send_nv_check(nvlist_add_boolean(header.get(), "raw"));
		auto filesystems = make_send_metadata();
		for (auto& pair : entries) {
			const std::string& name = pair.first;
			uint64_t parent_anchor = 0;
			if (name != root) {
				const auto parent = entries.find(name.substr(0, name.rfind('/')));
				if (parent == entries.end())
					invalid_selection("missing included parent for " + name);
				parent_anchor = parent->second.anchor;
			}
			send_nv_check(
			    nvlist_add_uint64(pair.second.metadata.get(), "parentfromsnap", parent_anchor));
			if (!pair.second.origin.empty()) {
				const auto origin_dataset = snapshot_dataset(pair.second.origin);
				const auto origin_entry = entries.find(origin_dataset);
				if (origin_entry != entries.end()) {
					nvlist_t* snaps = nullptr;
					uint64_t origin_guid = 0;
					send_nv_check(nvlist_lookup_nvlist(
					    origin_entry->second.metadata.get(), "snaps", &snaps));
					if (nvlist_lookup_uint64(
						snaps, snapshot_component(pair.second.origin).c_str(),
						&origin_guid) != 0)
						invalid_selection("clone origin must be included in the "
								  "snapshot catalogue: " +
								  pair.second.origin);
				} else if (origin_dataset == root ||
					   origin_dataset.compare(0, root.size() + 1, root + "/") == 0) {
					invalid_selection("clone origin dataset was excluded: " +
							  origin_dataset);
				}
			}
			std::ostringstream guid;
			guid << "0x" << std::hex << pair.second.guid;
			send_nv_check(nvlist_add_nvlist(filesystems.get(), guid.str().c_str(),
							pair.second.metadata.get()));
		}
		send_nv_check(nvlist_add_nvlist(header.get(), "fss", filesystems.get()));
		size_t packed_size = 0;
		send_nv_check(nvlist_size(header.get(), &packed_size, NV_ENCODE_XDR));
		if (packed_size > std::numeric_limits<uint32_t>::max())
			invalid_selection("replication metadata is too large");
		std::vector<char> packed(packed_size);
		char* buffer = packed.data();
		send_nv_check(nvlist_pack(header.get(), &buffer, &packed_size, NV_ENCODE_XDR, 0));
		return encode_compound_header(target.path(), packed.data(), packed_size);
	}

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
			, hierarchy_root_(impl_)
		{
			while (auto parent = hierarchy_root_->parent.lock())
				hierarchy_root_ = std::move(parent);
		}

		const std::string&
		Dataset::path() const noexcept
		{
			return impl_->name();
		}

		std::unique_ptr<Dataset>
		Dataset::parent() const
		{
			auto parent = impl_->parent.lock();
			if (!parent)
				return nullptr;
			if (parent->type() == ZFS_TYPE_VOLUME)
				return std::unique_ptr<Dataset>(new Volume(std::move(parent)));
			return std::unique_ptr<Dataset>(new Filesystem(std::move(parent)));
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

	std::uint64_t Dataset::Impl::number(zfs_prop_t property) const
	{
		DatasetHandle handle(open());
		std::uint64_t value = 0;
		if (zfs_prop_get_numeric(handle.get(), property, &value, nullptr,
								 nullptr, 0) != 0)
			detail::throw_libzfs_error(*context_, zfs_prop_to_name(property));
		return value;
	}

	const std::pair<std::uint64_t, std::uint64_t> &
	Dataset::Impl::identity() const
	{
		if (!identity_) {
			DatasetHandle handle(open());
			std::uint64_t guid = 0, creation = 0;
			if (zfs_prop_get_numeric(handle.get(), ZFS_PROP_GUID, &guid,
									 nullptr, nullptr, 0) != 0 ||
				zfs_prop_get_numeric(handle.get(), ZFS_PROP_CREATION, &creation,
									 nullptr, nullptr, 0) != 0)
				detail::throw_libzfs_error(*context_, "dataset identity");
			identity_ = std::make_pair(guid, creation);
		}
		return *identity_;
	}

	std::uint64_t Dataset::guid() const { return impl_->identity().first; }
	std::uint64_t Dataset::creation_time() const
	{
		return impl_->identity().second;
	}
	DatasetType Dataset::type() const noexcept
	{
		switch (impl_->type()) {
		case ZFS_TYPE_VOLUME:
			return DatasetType::volume;
		case ZFS_TYPE_SNAPSHOT:
			return DatasetType::snapshot;
		default:
			return DatasetType::filesystem;
		}
	}
	std::string Dataset::compression() const
	{
		return property("compression").value();
	}
	bool Dataset::readonly() const
	{
		return is_snapshot() || impl_->number(ZFS_PROP_READONLY) != 0;
	}
	std::uint64_t Dataset::allocated_space() const
	{
		return impl_->number(ZFS_PROP_USED);
	}
	std::uint64_t Dataset::free_space() const
	{
		return impl_->number(ZFS_PROP_AVAILABLE);
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
				std::shared_ptr<Dataset::Impl> parent;
				std::vector<Snapshot>* snapshots;
				std::exception_ptr* exception;
			} state { impl_->context(), impl_, &result, &exception };

			auto callback = [](zfs_handle_t* handle, void* arg) -> int {
				auto* state = static_cast<IteratorState*>(arg);

				try {
					const char* snapshot_name = zfs_get_name(handle);
					if (snapshot_name != nullptr) {
						auto snapshot_impl = std::make_shared<Dataset::Impl>(
							state->context, snapshot_name, ZFS_TYPE_SNAPSHOT);
						snapshot_impl->parent = state->parent;
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
			 * Sort by creation transaction while retaining the
			 * lightweight v2 iterator mode; close each
			 * iterator-owned handle immediately.
			 */
			const int error =
			    zfs_iter_snapshots_sorted_v2(dataset.get(), ZFS_ITER_SIMPLE, callback, &state, 0, 0);
			if (exception != nullptr)
				std::rethrow_exception(exception);
			if (error != 0)
				detail::throw_libzfs_error(*impl_->context(), "zfs_iter_snapshots_sorted_v2()");

			return result;
		}

	Snapshot
		Dataset::snapshot(const std::string& snapshot_name) const
		{
			if (is_snapshot() || snapshot_name.empty() ||
					snapshot_name.find_first_of("/@") != std::string::npos ||
					snapshot_name.find('\0') != std::string::npos) {
				throw Error(Error::Code::invalid_argument, 0,
						"snapshot lookup requires a relative snapshot name on a filesystem or volume");
			}

			const std::string full_name = name() + "@" + snapshot_name;
			DatasetHandle handle(zfs_open(impl_->context()->handle(),
					full_name.c_str(), ZFS_TYPE_SNAPSHOT));
			if (handle.get() == nullptr) {
				if (libzfs_errno(impl_->context()->handle()) == EZFS_NOENT) {
					throw Error(Error::Code::not_found, 0,
							"snapshot not found: " + full_name);
				}
				detail::throw_libzfs_error(*impl_->context(), full_name.c_str());
			}

			auto snapshot_impl = std::make_shared<Dataset::Impl>(
					impl_->context(), full_name, ZFS_TYPE_SNAPSHOT);
			snapshot_impl->parent = impl_;
			return Snapshot(std::move(snapshot_impl));
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
			snapshot_impl->parent = impl_;
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
				 * Build the complete filesystem/volume hierarchy in one discovery
				 * pass using lightweight handles. Each
				 * Dataset::Impl records its immediate children, so later children()
				 * calls are in-memory operations rather than additional libzfs walks.
				 */
				auto root_impl = std::make_shared<Dataset::Impl>(
						context, zfs_get_name(root), ZFS_TYPE_FILESYSTEM);
				result.filesystems_.push_back(Filesystem(root_impl));

				std::vector<std::shared_ptr<Dataset::Impl>> discovered;
				discovered.push_back(root_impl);
				using DatasetHandle = std::unique_ptr<zfs_handle_t, decltype(&zfs_close)>;
				std::vector<DatasetHandle> pending;
				std::exception_ptr exception;

				struct IteratorState {
					std::shared_ptr<detail::Context> context;
					DatasetCollection* result;
					std::vector<std::shared_ptr<Dataset::Impl>>* discovered;
					std::vector<DatasetHandle>* pending;
					std::exception_ptr* exception;
				} state { context, &result, &discovered, &pending, &exception };

				auto callback = [](zfs_handle_t* handle, void* arg) -> int {
					auto* state = static_cast<IteratorState*>(arg);
					DatasetHandle owned(handle, &zfs_close);
					try {
						const zfs_type_t type = zfs_get_type(handle);
						const char* dataset_name = zfs_get_name(handle);
						if (dataset_name == nullptr ||
								(type != ZFS_TYPE_FILESYSTEM && type != ZFS_TYPE_VOLUME))
							return 0;

						auto dataset_impl = std::make_shared<Dataset::Impl>(
								state->context, dataset_name, type);
						state->discovered->push_back(dataset_impl);
						if (type == ZFS_TYPE_FILESYSTEM) {
							state->result->filesystems_.push_back(Filesystem(dataset_impl));
							state->pending->push_back(std::move(owned));
						} else {
							state->result->volumes_.push_back(Volume(dataset_impl));
						}

						return 0;
					} catch (...) {
						*state->exception = std::current_exception();
						return 1;
					}
				};

				pending.emplace_back(root, &zfs_close);
				root = nullptr;
				for (std::size_t i = 0; i < pending.size(); ++i) {
					const int error = zfs_iter_filesystems_v2(pending[i].get(),
							ZFS_ITER_SIMPLE, callback, &state);
					if (exception != nullptr)
						std::rethrow_exception(exception);
					if (error != 0)
						detail::throw_libzfs_error(*context, "zfs_iter_filesystems_v2()");
					pending[i].reset();
				}

				std::unordered_map<std::string, std::shared_ptr<Dataset::Impl>> by_name;
				by_name.reserve(discovered.size());
				for (const auto& dataset : discovered)
					by_name.emplace(dataset->name(), dataset);

				for (std::size_t i = 1; i < discovered.size(); ++i) {
					const auto& dataset = discovered[i];
					const std::string& dataset_name = dataset->name();
					const std::size_t slash = dataset_name.rfind('/');
					if (slash == std::string::npos)
						continue;

					const auto parent = by_name.find(dataset_name.substr(0, slash));
					if (parent != by_name.end() && parent->second != dataset)
					{
						parent->second->add_child(dataset);
						dataset->parent = parent->second;
					}
				}
				for (auto& filesystem : result.filesystems_)
					filesystem.hierarchy_root_ = root_impl;
				for (auto& volume : result.volumes_)
					volume.hierarchy_root_ = root_impl;
			} catch (...) {
				if (root != nullptr)
					zfs_close(root);
				throw;
			}

			return result;
		}

	std::unique_ptr<Dataset>
		Pool::dataset(const std::string& dataset_name) const
		{
			const std::string pool_name = name();
			const bool in_pool = dataset_name == pool_name ||
					(dataset_name.size() > pool_name.size() &&
					 dataset_name.compare(0, pool_name.size(), pool_name) == 0 &&
					 (dataset_name[pool_name.size()] == '/' ||
					  dataset_name[pool_name.size()] == '@'));
			if (dataset_name.empty() || dataset_name.find('\0') != std::string::npos ||
					!in_pool) {
				throw Error(Error::Code::invalid_argument, 0,
						"dataset name must belong to pool: " + pool_name);
			}

			if (dataset_name.find('@') == std::string::npos) {
				auto all = datasets();
				for (auto& filesystem : all.filesystems_) {
					if (filesystem.name() == dataset_name)
						return std::make_unique<Filesystem>(std::move(filesystem));
				}
				for (auto& volume : all.volumes_) {
					if (volume.name() == dataset_name)
						return std::make_unique<Volume>(std::move(volume));
				}
				throw Error(Error::Code::not_found, 0,
						"dataset not found: " + dataset_name);
			}

			const auto separator = dataset_name.find('@');
			auto source = dataset(dataset_name.substr(0, separator));
			return std::make_unique<Snapshot>(
				source->snapshot(dataset_name.substr(separator + 1)));
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
