#include "internal/ZFSInternal.hpp"

#include <exception>
#include <functional>
#include <utility>

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

	zfs_handle_t*
		Dataset::Impl::open() const
		{
			zfs_handle_t* handle = zfs_open(context_->handle(), name_.c_str(), type_);
			if (handle == nullptr)
				detail::throw_libzfs_error(*context_, name_.c_str());
			return handle;
		}

	namespace {

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

	Dataset::Dataset(std::shared_ptr<Impl> impl) noexcept
		: impl_(std::move(impl))
		{
		}

	Dataset::Dataset(Dataset&&) noexcept = default;
	Dataset& Dataset::operator=(Dataset&&) noexcept = default;
	Dataset::~Dataset() = default;

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

	Filesystem::Filesystem(std::shared_ptr<Impl> impl) noexcept
		: Dataset(std::move(impl))
		{
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

	const std::vector<Filesystem>&
		DatasetCollection::filesystems() const noexcept
		{
			return filesystems_;
		}

	const std::vector<Volume>&
		DatasetCollection::volumes() const noexcept
		{
			return volumes_;
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
				auto root_impl = std::make_shared<Dataset::Impl>(
						context, zfs_get_name(root), ZFS_TYPE_FILESYSTEM);
				result.filesystems_.push_back(Filesystem(root_impl));

				std::function<int(zfs_handle_t*)> visit;
				struct IteratorState {
					std::function<int(zfs_handle_t*)>* visit;
					std::exception_ptr exception;
				} state { &visit, nullptr };

				auto callback = [](zfs_handle_t* handle, void* arg) -> int {
					auto* state = static_cast<IteratorState*>(arg);
					return (*state->visit)(handle);
				};

				visit = [&](zfs_handle_t* handle) -> int {
					try {
						const zfs_type_t type = zfs_get_type(handle);
						const char* dataset_name = zfs_get_name(handle);
						if (dataset_name == nullptr) {
							zfs_close(handle);
							return 0;
						}

						if (type == ZFS_TYPE_FILESYSTEM) {
							auto dataset_impl = std::make_shared<Dataset::Impl>(
									context, dataset_name, type);
							result.filesystems_.push_back(Filesystem(dataset_impl));
							const int error = zfs_iter_filesystems_v2(handle,
									ZFS_ITER_SIMPLE, callback, &state);
							zfs_close(handle);
							return error;
						}

						if (type == ZFS_TYPE_VOLUME) {
							auto dataset_impl = std::make_shared<Dataset::Impl>(
									context, dataset_name, type);
							result.volumes_.push_back(Volume(dataset_impl));
						}

						zfs_close(handle);
						return 0;
					} catch (...) {
						zfs_close(handle);
						state.exception = std::current_exception();
						return 1;
					}
				};

				const int error = zfs_iter_filesystems_v2(root, ZFS_ITER_SIMPLE,
						callback, &state);
				zfs_close(root);
				root = nullptr;

				if (state.exception != nullptr)
					std::rethrow_exception(state.exception);
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

} // namespace zfs
