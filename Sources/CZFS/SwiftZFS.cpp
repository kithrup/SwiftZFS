#include "SwiftZFS.h"

#include "ZFS.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

struct swiftzfs_error {
	swiftzfs_error_code_t code = SWIFTZFS_ERROR_UNKNOWN;
	int32_t system_error = 0;
	std::string message;
};

struct swiftzfs_context {
	zfs::ZFS impl;
};

struct swiftzfs_pool {
	explicit swiftzfs_pool(zfs::Pool pool) : impl(std::move(pool)) {}
	zfs::Pool impl;
};

struct swiftzfs_pool_list {
	std::vector<std::unique_ptr<swiftzfs_pool>> pools;
};

struct swiftzfs_dataset {
	explicit swiftzfs_dataset(std::unique_ptr<zfs::Dataset> dataset)
		: impl(std::move(dataset))
	{
	}

	std::unique_ptr<zfs::Dataset> impl;
};

struct swiftzfs_dataset_list {
	std::vector<std::unique_ptr<swiftzfs_dataset>> datasets;
};

struct swiftzfs_property {
	explicit swiftzfs_property(zfs::Property property)
		: impl(std::move(property))
	{
	}

	zfs::Property impl;
};

struct swiftzfs_property_list {
	explicit swiftzfs_property_list(std::vector<zfs::Property> properties)
		: properties(std::move(properties))
	{
	}

	std::vector<zfs::Property> properties;
};

struct swiftzfs_send_stream {
	explicit swiftzfs_send_stream(zfs::SendStream stream)
		: impl(std::move(stream))
	{
	}

	zfs::SendStream impl;
};

namespace {

	swiftzfs_error_code_t
		map_error_code(zfs::Error::Code code) noexcept
		{
			switch (code) {
				case zfs::Error::Code::unknown:
					return SWIFTZFS_ERROR_UNKNOWN;
				case zfs::Error::Code::initialization_failed:
					return SWIFTZFS_ERROR_INITIALIZATION_FAILED;
				case zfs::Error::Code::invalid_argument:
					return SWIFTZFS_ERROR_INVALID_ARGUMENT;
				case zfs::Error::Code::permission_denied:
					return SWIFTZFS_ERROR_PERMISSION_DENIED;
				case zfs::Error::Code::not_found:
					return SWIFTZFS_ERROR_NOT_FOUND;
				case zfs::Error::Code::already_exists:
					return SWIFTZFS_ERROR_ALREADY_EXISTS;
				case zfs::Error::Code::io_error:
					return SWIFTZFS_ERROR_IO;
				case zfs::Error::Code::no_memory:
					return SWIFTZFS_ERROR_NO_MEMORY;
				case zfs::Error::Code::no_such_pool:
					return SWIFTZFS_ERROR_NO_SUCH_POOL;
				case zfs::Error::Code::pool_unavailable:
					return SWIFTZFS_ERROR_POOL_UNAVAILABLE;
				case zfs::Error::Code::pool_busy:
					return SWIFTZFS_ERROR_POOL_BUSY;
				case zfs::Error::Code::pool_faulted:
					return SWIFTZFS_ERROR_POOL_FAULTED;
				case zfs::Error::Code::invalid_pool:
					return SWIFTZFS_ERROR_INVALID_POOL;
				case zfs::Error::Code::unsupported:
					return SWIFTZFS_ERROR_UNSUPPORTED;
				case zfs::Error::Code::not_implemented:
					return SWIFTZFS_ERROR_NOT_IMPLEMENTED;
			}
			return SWIFTZFS_ERROR_UNKNOWN;
		}

	swiftzfs_property_source_t
		map_property_source(zfs::PropertySource source) noexcept
		{
			switch (source) {
				case zfs::PropertySource::none:
					return SWIFTZFS_PROPERTY_SOURCE_NONE;
				case zfs::PropertySource::default_value:
					return SWIFTZFS_PROPERTY_SOURCE_DEFAULT;
				case zfs::PropertySource::temporary:
					return SWIFTZFS_PROPERTY_SOURCE_TEMPORARY;
				case zfs::PropertySource::local:
					return SWIFTZFS_PROPERTY_SOURCE_LOCAL;
				case zfs::PropertySource::inherited:
					return SWIFTZFS_PROPERTY_SOURCE_INHERITED;
				case zfs::PropertySource::received:
					return SWIFTZFS_PROPERTY_SOURCE_RECEIVED;
				case zfs::PropertySource::unknown:
					return SWIFTZFS_PROPERTY_SOURCE_UNKNOWN;
			}
			return SWIFTZFS_PROPERTY_SOURCE_UNKNOWN;
		}

	swiftzfs_property_type_t
		map_property_type(zfs::PropertyType type) noexcept
		{
			switch (type) {
				case zfs::PropertyType::number:
					return SWIFTZFS_PROPERTY_NUMBER;
				case zfs::PropertyType::string:
					return SWIFTZFS_PROPERTY_STRING;
				case zfs::PropertyType::index:
					return SWIFTZFS_PROPERTY_INDEX;
				case zfs::PropertyType::unknown:
					return SWIFTZFS_PROPERTY_UNKNOWN;
			}
			return SWIFTZFS_PROPERTY_UNKNOWN;
		}

	void
		clear_error(swiftzfs_error_t **error) noexcept
		{
			if (error != nullptr) {
				*error = nullptr;
			}
		}

	void
		set_error(swiftzfs_error_t **error, swiftzfs_error_code_t code,
				int32_t system_error, const char *message) noexcept
		{
			if (error == nullptr) {
				return;
			}

			try {
				auto value = std::make_unique<swiftzfs_error>();
				value->code = code;
				value->system_error = system_error;
				value->message = message != nullptr ? message : "";
				*error = value.release();
			} catch (...) {
				*error = nullptr;
			}
		}

	swiftzfs_status_t
		translate_current_exception(swiftzfs_error_t **error) noexcept
		{
			try {
				throw;
			} catch (const zfs::Error &e) {
				set_error(error, map_error_code(e.code()), e.system_error(), e.what());
				return e.code() == zfs::Error::Code::no_memory ? SWIFTZFS_NO_MEMORY
					: SWIFTZFS_ERROR;
			} catch (const std::bad_alloc &) {
				set_error(error, SWIFTZFS_ERROR_NO_MEMORY, 0, "out of memory");
				return SWIFTZFS_NO_MEMORY;
			} catch (const std::exception &e) {
				set_error(error, SWIFTZFS_ERROR_UNKNOWN, 0, e.what());
				return SWIFTZFS_ERROR;
			} catch (...) {
				set_error(error, SWIFTZFS_ERROR_UNKNOWN, 0, "unknown C++ exception");
				return SWIFTZFS_ERROR;
			}
		}

	swiftzfs_status_t
		invalid_argument(swiftzfs_error_t **error, const char *message) noexcept
		{
			set_error(error, SWIFTZFS_ERROR_INVALID_ARGUMENT, 0, message);
			return SWIFTZFS_INVALID_ARGUMENT;
		}

	char *
		copy_string(const std::string &value)
		{
			auto *result = static_cast<char *>(std::malloc(value.size() + 1));
			if (result == nullptr) {
				throw std::bad_alloc();
			}
			std::memcpy(result, value.c_str(), value.size() + 1);
			return result;
		}

	std::unique_ptr<zfs::Dataset>
		make_dataset(zfs::Filesystem filesystem)
		{
			return std::make_unique<zfs::Filesystem>(std::move(filesystem));
		}

	std::unique_ptr<zfs::Dataset>
		make_dataset(zfs::Volume volume)
		{
			return std::make_unique<zfs::Volume>(std::move(volume));
		}

	std::unique_ptr<zfs::Dataset>
		make_dataset(zfs::Snapshot snapshot)
		{
			return std::make_unique<zfs::Snapshot>(std::move(snapshot));
		}

	swiftzfs_dataset_type_t
		dataset_type(const zfs::Dataset &dataset) noexcept
		{
			if (dataset.is_filesystem()) {
				return SWIFTZFS_DATASET_FILESYSTEM;
			}
			if (dataset.is_volume()) {
				return SWIFTZFS_DATASET_VOLUME;
			}
			if (dataset.is_snapshot()) {
				return SWIFTZFS_DATASET_SNAPSHOT;
			}
			return SWIFTZFS_DATASET_UNKNOWN;
		}

	zfs::Filesystem *
		as_filesystem(const swiftzfs_dataset_t *dataset) noexcept
		{
			if (dataset == nullptr || dataset->impl == nullptr) {
				return nullptr;
			}
			return dynamic_cast<zfs::Filesystem *>(dataset->impl.get());
		}

	zfs::Volume *
		as_volume(const swiftzfs_dataset_t *dataset) noexcept
		{
			if (dataset == nullptr || dataset->impl == nullptr) {
				return nullptr;
			}
			return dynamic_cast<zfs::Volume *>(dataset->impl.get());
		}

	zfs::Snapshot *
		as_snapshot(const swiftzfs_dataset_t *dataset) noexcept
		{
			if (dataset == nullptr || dataset->impl == nullptr) {
				return nullptr;
			}
			return dynamic_cast<zfs::Snapshot *>(dataset->impl.get());
		}

	zfs::SendOptions
		send_options(const swiftzfs_send_options_t *options) noexcept
		{
			zfs::SendOptions result;
			if (options != nullptr) {
				result.embedded_data = options->embedded_data;
				result.large_blocks = options->large_blocks;
				result.compressed = options->compressed;
				result.raw = options->raw;
			}
			return result;
		}

	zfs::PropertyValues
		property_values(const swiftzfs_property_value_t *properties, size_t count)
		{
			zfs::PropertyValues result;
			if (count != 0 && properties == nullptr) {
				throw zfs::Error(zfs::Error::Code::invalid_argument, 0,
						"property array is null but property_count is non-zero");
			}

			for (size_t i = 0; i < count; ++i) {
				if (properties[i].name == nullptr || properties[i].value == nullptr) {
					throw zfs::Error(zfs::Error::Code::invalid_argument, 0,
							"snapshot property name and value must not be null");
				}
				result[properties[i].name] = properties[i].value;
			}
			return result;
		}

} // namespace

	extern "C" void
swiftzfs_error_destroy(swiftzfs_error_t *error)
{
	delete error;
}

	extern "C" swiftzfs_error_code_t
swiftzfs_error_code(const swiftzfs_error_t *error)
{
	return error != nullptr ? error->code : SWIFTZFS_ERROR_UNKNOWN;
}

	extern "C" int32_t
swiftzfs_error_system_error(const swiftzfs_error_t *error)
{
	return error != nullptr ? error->system_error : 0;
}

	extern "C" const char *
swiftzfs_error_message(const swiftzfs_error_t *error)
{
	return error != nullptr ? error->message.c_str() : "";
}

	extern "C" void
swiftzfs_string_free(char *string)
{
	std::free(string);
}

	extern "C" swiftzfs_status_t
swiftzfs_context_create(swiftzfs_context_t **result, swiftzfs_error_t **error)
{
	clear_error(error);
	if (result == nullptr) {
		return invalid_argument(error, "result must not be null");
	}
	*result = nullptr;

	try {
		*result = new swiftzfs_context;
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" void
swiftzfs_context_destroy(swiftzfs_context_t *context)
{
	delete context;
}

	extern "C" swiftzfs_status_t
swiftzfs_context_pools(swiftzfs_context_t *context,
		swiftzfs_pool_list_t **result, swiftzfs_error_t **error)
{
	clear_error(error);
	if (context == nullptr || result == nullptr) {
		return invalid_argument(error, "context and result must not be null");
	}
	*result = nullptr;

	try {
		auto list = std::make_unique<swiftzfs_pool_list>();
		auto pools = context->impl.pools();
		list->pools.reserve(pools.size());
		for (auto &pool : pools) {
			list->pools.push_back(
					std::make_unique<swiftzfs_pool>(std::move(pool)));
		}
		*result = list.release();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" void
swiftzfs_pool_list_destroy(swiftzfs_pool_list_t *list)
{
	delete list;
}

	extern "C" size_t
swiftzfs_pool_list_count(const swiftzfs_pool_list_t *list)
{
	return list != nullptr ? list->pools.size() : 0;
}

	extern "C" swiftzfs_pool_t *
swiftzfs_pool_list_take_at(swiftzfs_pool_list_t *list, size_t index)
{
	if (list == nullptr || index >= list->pools.size()) {
		return nullptr;
	}
	return list->pools[index].release();
}

	extern "C" void
swiftzfs_pool_destroy(swiftzfs_pool_t *pool)
{
	delete pool;
}

	extern "C" swiftzfs_status_t
swiftzfs_pool_copy_name(const swiftzfs_pool_t *pool, char **result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	if (pool == nullptr || result == nullptr) {
		return invalid_argument(error, "pool and result must not be null");
	}
	*result = nullptr;

	try {
		*result = copy_string(pool->impl.name());
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_pool_guid(const swiftzfs_pool_t *pool, uint64_t *result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	if (pool == nullptr || result == nullptr) {
		return invalid_argument(error, "pool and result must not be null");
	}

	try {
		*result = pool->impl.guid();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_pool_properties(const swiftzfs_pool_t *pool,
		swiftzfs_property_list_t **result, swiftzfs_error_t **error)
{
	clear_error(error);
	if (pool == nullptr || result == nullptr) {
		return invalid_argument(error, "pool and result must not be null");
	}
	*result = nullptr;

	try {
		*result = new swiftzfs_property_list(pool->impl.properties());
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_dataset_properties(const swiftzfs_dataset_t *dataset,
		swiftzfs_property_list_t **result, swiftzfs_error_t **error)
{
	clear_error(error);
	if (dataset == nullptr || dataset->impl == nullptr || result == nullptr) {
		return invalid_argument(error, "dataset and result must not be null");
	}
	*result = nullptr;

	try {
		*result = new swiftzfs_property_list(dataset->impl->properties());
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_dataset_property(const swiftzfs_dataset_t *dataset, const char *name,
		swiftzfs_property_t **result, swiftzfs_error_t **error)
{
	clear_error(error);
	if (dataset == nullptr || dataset->impl == nullptr || name == nullptr ||
			result == nullptr) {
		return invalid_argument(error,
				"dataset, property name, and result must not be null");
	}
	*result = nullptr;

	try {
		*result = new swiftzfs_property(dataset->impl->property(name));
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" void
swiftzfs_property_destroy(swiftzfs_property_t *property)
{
	delete property;
}

	extern "C" const char *
swiftzfs_property_name(const swiftzfs_property_t *property)
{
	return property != nullptr ? property->impl.name().c_str() : nullptr;
}

	extern "C" const char *
swiftzfs_property_value(const swiftzfs_property_t *property)
{
	return property != nullptr ? property->impl.value().c_str() : nullptr;
}

	extern "C" swiftzfs_property_source_t
swiftzfs_property_source(const swiftzfs_property_t *property)
{
	return property != nullptr ? map_property_source(property->impl.source())
		: SWIFTZFS_PROPERTY_SOURCE_UNKNOWN;
}

	extern "C" swiftzfs_property_type_t
swiftzfs_property_type(const swiftzfs_property_t *property)
{
	return property != nullptr ? map_property_type(property->impl.type())
		: SWIFTZFS_PROPERTY_UNKNOWN;
}

	extern "C" bool
swiftzfs_property_readonly(const swiftzfs_property_t *property)
{
	return property != nullptr && property->impl.readonly();
}

	extern "C" void
swiftzfs_property_list_destroy(swiftzfs_property_list_t *list)
{
	delete list;
}

	extern "C" size_t
swiftzfs_property_list_count(const swiftzfs_property_list_t *list)
{
	return list != nullptr ? list->properties.size() : 0;
}

	extern "C" const char *
swiftzfs_property_list_name_at(const swiftzfs_property_list_t *list,
		size_t index)
{
	return list != nullptr && index < list->properties.size()
		? list->properties[index].name().c_str()
		: nullptr;
}

	extern "C" const char *
swiftzfs_property_list_value_at(const swiftzfs_property_list_t *list,
		size_t index)
{
	return list != nullptr && index < list->properties.size()
		? list->properties[index].value().c_str()
		: nullptr;
}

	extern "C" swiftzfs_property_source_t
swiftzfs_property_list_source_at(const swiftzfs_property_list_t *list,
		size_t index)
{
	return list != nullptr && index < list->properties.size()
		? map_property_source(list->properties[index].source())
		: SWIFTZFS_PROPERTY_SOURCE_UNKNOWN;
}

	extern "C" swiftzfs_property_type_t
swiftzfs_property_list_type_at(const swiftzfs_property_list_t *list,
		size_t index)
{
	return list != nullptr && index < list->properties.size()
		? map_property_type(list->properties[index].type())
		: SWIFTZFS_PROPERTY_UNKNOWN;
}

	extern "C" bool
swiftzfs_property_list_readonly_at(const swiftzfs_property_list_t *list,
		size_t index)
{
	return list != nullptr && index < list->properties.size() &&
		list->properties[index].readonly();
}

	extern "C" swiftzfs_status_t
swiftzfs_pool_datasets(const swiftzfs_pool_t *pool,
		swiftzfs_dataset_list_t **result, swiftzfs_error_t **error)
{
	clear_error(error);
	if (pool == nullptr || result == nullptr) {
		return invalid_argument(error, "pool and result must not be null");
	}
	*result = nullptr;

	try {
		auto datasets = pool->impl.datasets();
		auto filesystems = std::move(datasets).filesystems();
		auto volumes = std::move(datasets).volumes();
		auto list = std::make_unique<swiftzfs_dataset_list>();
		list->datasets.reserve(filesystems.size() + volumes.size());
		for (auto &filesystem : filesystems) {
			list->datasets.push_back(std::make_unique<swiftzfs_dataset>(
						make_dataset(std::move(filesystem))));
		}
		for (auto &volume : volumes) {
			list->datasets.push_back(std::make_unique<swiftzfs_dataset>(
						make_dataset(std::move(volume))));
		}

		*result = list.release();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" void
swiftzfs_dataset_list_destroy(swiftzfs_dataset_list_t *list)
{
	delete list;
}

	extern "C" size_t
swiftzfs_dataset_list_count(const swiftzfs_dataset_list_t *list)
{
	return list != nullptr ? list->datasets.size() : 0;
}

	extern "C" swiftzfs_dataset_t *
swiftzfs_dataset_list_take_at(swiftzfs_dataset_list_t *list, size_t index)
{
	if (list == nullptr || index >= list->datasets.size()) {
		return nullptr;
	}
	return list->datasets[index].release();
}

	extern "C" void
swiftzfs_dataset_destroy(swiftzfs_dataset_t *dataset)
{
	delete dataset;
}

	extern "C" swiftzfs_dataset_type_t
swiftzfs_dataset_type(const swiftzfs_dataset_t *dataset)
{
	return dataset != nullptr && dataset->impl != nullptr
		? dataset_type(*dataset->impl)
		: SWIFTZFS_DATASET_UNKNOWN;
}

	extern "C" swiftzfs_status_t
swiftzfs_dataset_copy_name(const swiftzfs_dataset_t *dataset, char **result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	if (dataset == nullptr || dataset->impl == nullptr || result == nullptr) {
		return invalid_argument(error, "dataset and result must not be null");
	}
	*result = nullptr;

	try {
		*result = copy_string(dataset->impl->name());
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_dataset_guid(const swiftzfs_dataset_t *dataset, uint64_t *result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	if (dataset == nullptr || dataset->impl == nullptr || result == nullptr) {
		return invalid_argument(error, "dataset and result must not be null");
	}

	try {
		*result = dataset->impl->guid();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_filesystem_mounted(const swiftzfs_dataset_t *filesystem,
		bool *result, swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_filesystem(filesystem);
	if (value == nullptr || result == nullptr) {
		return invalid_argument(error,
				"dataset must be a filesystem and result must not be null");
	}

	try {
		*result = value->mounted();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_filesystem_copy_mountpoint(const swiftzfs_dataset_t *filesystem,
		char **result, swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_filesystem(filesystem);
	if (value == nullptr || result == nullptr) {
		return invalid_argument(error,
				"dataset must be a filesystem and result must not be null");
	}
	*result = nullptr;

	try {
		*result = copy_string(value->mountpoint());
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_volume_size(const swiftzfs_dataset_t *volume, uint64_t *result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_volume(volume);
	if (value == nullptr || result == nullptr) {
		return invalid_argument(error,
				"dataset must be a volume and result must not be null");
	}

	try {
		*result = value->size();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_volume_block_size(const swiftzfs_dataset_t *volume,
		uint64_t *result, swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_volume(volume);
	if (value == nullptr || result == nullptr) {
		return invalid_argument(error,
				"dataset must be a volume and result must not be null");
	}

	try {
		*result = value->block_size();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_dataset_snapshots(const swiftzfs_dataset_t *dataset,
		swiftzfs_dataset_list_t **result, swiftzfs_error_t **error)
{
	clear_error(error);
	if (dataset == nullptr || dataset->impl == nullptr || result == nullptr) {
		return invalid_argument(error, "dataset and result must not be null");
	}
	*result = nullptr;

	try {
		auto snapshots = dataset->impl->snapshots();
		auto list = std::make_unique<swiftzfs_dataset_list>();
		list->datasets.reserve(snapshots.size());
		for (auto &snapshot : snapshots) {
			list->datasets.push_back(std::make_unique<swiftzfs_dataset>(
						make_dataset(std::move(snapshot))));
		}
		*result = list.release();
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_dataset_create_snapshot(const swiftzfs_dataset_t *dataset,
		const char *snapshot_name, const swiftzfs_property_value_t *properties,
		size_t property_count, swiftzfs_dataset_t **result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	if (dataset == nullptr || dataset->impl == nullptr ||
			snapshot_name == nullptr || result == nullptr) {
		return invalid_argument(error,
				"dataset, snapshot name, and result must not be null");
	}
	*result = nullptr;

	try {
		auto snapshot = dataset->impl->create_snapshot(
				snapshot_name, property_values(properties, property_count));
		*result = new swiftzfs_dataset(make_dataset(std::move(snapshot)));
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_snapshot_send_stream(const swiftzfs_dataset_t *snapshot,
		const swiftzfs_send_options_t *options, swiftzfs_send_stream_t **result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_snapshot(snapshot);
	if (value == nullptr || result == nullptr) {
		return invalid_argument(error,
				"snapshot and result must not be null");
	}
	*result = nullptr;

	try {
		*result = new swiftzfs_send_stream(value->send(send_options(options)));
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_snapshot_send_stream_incremental(const swiftzfs_dataset_t *snapshot,
		const swiftzfs_dataset_t *from_snapshot,
		const swiftzfs_send_options_t *options, swiftzfs_send_stream_t **result,
		swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_snapshot(snapshot);
	auto *from = as_snapshot(from_snapshot);
	if (value == nullptr || from == nullptr || result == nullptr) {
		return invalid_argument(error,
				"target, starting snapshot, and result must not be null");
	}
	*result = nullptr;

	try {
		*result = new swiftzfs_send_stream(
				value->send(*from, send_options(options)));
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" void
swiftzfs_send_stream_destroy(swiftzfs_send_stream_t *stream)
{
	delete stream;
}

	extern "C" swiftzfs_status_t
swiftzfs_send_stream_read(swiftzfs_send_stream_t *stream, void *buffer,
		size_t capacity, size_t *bytes_read, swiftzfs_error_t **error)
{
	clear_error(error);
	if (stream == nullptr || bytes_read == nullptr ||
			(buffer == nullptr && capacity != 0)) {
		return invalid_argument(error,
				"stream, bytes_read, and non-empty buffer must not be null");
	}
	*bytes_read = 0;

	try {
		*bytes_read = stream->impl.read(buffer, capacity);
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_snapshot_send(const swiftzfs_dataset_t *snapshot, int fd,
		const swiftzfs_send_options_t *options, swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_snapshot(snapshot);
	if (value == nullptr) {
		return invalid_argument(error, "dataset must be a snapshot");
	}

	try {
		value->send_to(fd, send_options(options));
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}

	extern "C" swiftzfs_status_t
swiftzfs_snapshot_send_incremental(const swiftzfs_dataset_t *snapshot,
		const swiftzfs_dataset_t *from_snapshot, int fd,
		const swiftzfs_send_options_t *options, swiftzfs_error_t **error)
{
	clear_error(error);
	auto *value = as_snapshot(snapshot);
	auto *from = as_snapshot(from_snapshot);
	if (value == nullptr || from == nullptr) {
		return invalid_argument(error,
				"target and starting datasets must both be snapshots");
	}

	try {
		value->send_to(fd, *from, send_options(options));
		return SWIFTZFS_OK;
	} catch (...) {
		return translate_current_exception(error);
	}
}
