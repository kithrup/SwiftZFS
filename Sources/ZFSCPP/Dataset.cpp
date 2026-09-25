#include "internal/ZFSInternal.hpp"

#include <libnvpair.h>
#include <zfs_prop.h>

#include <cstring>
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

	DatasetProperty::DatasetProperty(std::string name, std::string value,
			PropertySource source, PropertyType type, bool readonly)
		: name_(std::move(name)), value_(std::move(value)), source_(source),
		type_(type), readonly_(readonly)
	{
	}

	const std::string&
		DatasetProperty::name() const noexcept
		{
			return name_;
		}

	const std::string&
		DatasetProperty::value() const noexcept
		{
			return value_;
		}

	PropertySource
		DatasetProperty::source() const noexcept
		{
			return source_;
		}

	PropertyType
		DatasetProperty::type() const noexcept
		{
			return type_;
		}

	bool
		DatasetProperty::readonly() const noexcept
		{
			return readonly_;
		}

	std::vector<DatasetProperty>
		Dataset::properties() const
		{
			DatasetHandle dataset(impl_->open());
			zfs_handle_t* handle = dataset.get();
			const zfs_type_t dataset_type = zfs_get_type(handle);
			std::vector<DatasetProperty> result;
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

				result.push_back(DatasetProperty(zfs_prop_to_name(prop), value,
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
				result.push_back(DatasetProperty(nvpair_name(pair), value_string,
							user_property_source(source_string, zfs_get_name(handle)),
							PropertyType::string, false));
			}

			return result;
		}

	DatasetProperty
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
				return DatasetProperty(name, value, property_source(source),
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
					return DatasetProperty(name, value_string,
							user_property_source(source_string, zfs_get_name(handle)),
							PropertyType::string, false);
				}
			}

			throw Error(Error::Code::not_found, 0,
					"dataset property not found: " + name);
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
