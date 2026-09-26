#ifndef SWIFTZFS_H
#define SWIFTZFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	/*
	 * Stable C ABI used by the Swift layer.
	 *
	 * This header deliberately exposes no C++, STL, libzfs, libzfs_core, libzpool,
	 * nvlist, or other OpenZFS-specific types.  Unless documented otherwise,
	 * opaque objects returned through an out parameter are owned by the caller and
	 * must be released with the matching swiftzfs_*_destroy() function.
	 */

	typedef struct swiftzfs_context swiftzfs_context_t;
	typedef struct swiftzfs_pool swiftzfs_pool_t;
	typedef struct swiftzfs_pool_list swiftzfs_pool_list_t;
	typedef struct swiftzfs_dataset swiftzfs_dataset_t;
	typedef struct swiftzfs_dataset_list swiftzfs_dataset_list_t;
	typedef struct swiftzfs_property swiftzfs_property_t;
	typedef struct swiftzfs_property_list swiftzfs_property_list_t;
	typedef struct swiftzfs_send_stream swiftzfs_send_stream_t;
	typedef struct swiftzfs_error swiftzfs_error_t;

	typedef enum swiftzfs_status {
		SWIFTZFS_OK = 0,
		SWIFTZFS_ERROR = -1,
		SWIFTZFS_NO_MEMORY = -2,
		SWIFTZFS_INVALID_ARGUMENT = -3
	} swiftzfs_status_t;

	typedef enum swiftzfs_error_code {
		SWIFTZFS_ERROR_UNKNOWN = 0,
		SWIFTZFS_ERROR_INITIALIZATION_FAILED,
		SWIFTZFS_ERROR_INVALID_ARGUMENT,
		SWIFTZFS_ERROR_PERMISSION_DENIED,
		SWIFTZFS_ERROR_NOT_FOUND,
		SWIFTZFS_ERROR_ALREADY_EXISTS,
		SWIFTZFS_ERROR_IO,
		SWIFTZFS_ERROR_NO_MEMORY,
		SWIFTZFS_ERROR_NO_SUCH_POOL,
		SWIFTZFS_ERROR_POOL_UNAVAILABLE,
		SWIFTZFS_ERROR_POOL_BUSY,
		SWIFTZFS_ERROR_POOL_FAULTED,
		SWIFTZFS_ERROR_INVALID_POOL,
		SWIFTZFS_ERROR_UNSUPPORTED,
		SWIFTZFS_ERROR_NOT_IMPLEMENTED
	} swiftzfs_error_code_t;

	typedef enum swiftzfs_dataset_type {
		SWIFTZFS_DATASET_UNKNOWN = 0,
		SWIFTZFS_DATASET_FILESYSTEM,
		SWIFTZFS_DATASET_VOLUME,
		SWIFTZFS_DATASET_SNAPSHOT
	} swiftzfs_dataset_type_t;

	typedef enum swiftzfs_property_source {
		SWIFTZFS_PROPERTY_SOURCE_NONE = 0,
		SWIFTZFS_PROPERTY_SOURCE_DEFAULT,
		SWIFTZFS_PROPERTY_SOURCE_TEMPORARY,
		SWIFTZFS_PROPERTY_SOURCE_LOCAL,
		SWIFTZFS_PROPERTY_SOURCE_INHERITED,
		SWIFTZFS_PROPERTY_SOURCE_RECEIVED,
		SWIFTZFS_PROPERTY_SOURCE_UNKNOWN
	} swiftzfs_property_source_t;

	typedef enum swiftzfs_property_type {
		SWIFTZFS_PROPERTY_NUMBER = 0,
		SWIFTZFS_PROPERTY_STRING,
		SWIFTZFS_PROPERTY_INDEX,
		SWIFTZFS_PROPERTY_UNKNOWN
	} swiftzfs_property_type_t;

	typedef struct swiftzfs_property_value {
		const char *name;
		const char *value;
	} swiftzfs_property_value_t;

	typedef struct swiftzfs_send_options {
		bool embedded_data;
		bool large_blocks;
		bool compressed;
		bool raw;
	} swiftzfs_send_options_t;

	/* Error objects. */
	void swiftzfs_error_destroy(swiftzfs_error_t *error);
	swiftzfs_error_code_t swiftzfs_error_code(const swiftzfs_error_t *error);
	int32_t swiftzfs_error_system_error(const swiftzfs_error_t *error);
	const char *swiftzfs_error_message(const swiftzfs_error_t *error);

	/* Strings returned by *_copy_* functions are released here. */
	void swiftzfs_string_free(char *string);

	/* Top-level ZFS context. */
	swiftzfs_status_t swiftzfs_context_create(
			swiftzfs_context_t **result,
			swiftzfs_error_t **error);
	void swiftzfs_context_destroy(swiftzfs_context_t *context);

	/* Imported pools. */
	swiftzfs_status_t swiftzfs_context_pools(
			swiftzfs_context_t *context,
			swiftzfs_pool_list_t **result,
			swiftzfs_error_t **error);
	void swiftzfs_pool_list_destroy(swiftzfs_pool_list_t *list);
	size_t swiftzfs_pool_list_count(const swiftzfs_pool_list_t *list);

	/*
	 * Transfer ownership of one pool out of a list.  The returned pool must be
	 * destroyed by the caller.  A second call for the same index returns NULL.
	 */
	swiftzfs_pool_t *swiftzfs_pool_list_take_at(
			swiftzfs_pool_list_t *list,
			size_t index);

	void swiftzfs_pool_destroy(swiftzfs_pool_t *pool);
	swiftzfs_status_t swiftzfs_pool_copy_name(
			const swiftzfs_pool_t *pool,
			char **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_pool_guid(
			const swiftzfs_pool_t *pool,
			uint64_t *result,
			swiftzfs_error_t **error);

	/* Pool and dataset properties. */
	swiftzfs_status_t swiftzfs_pool_properties(
			const swiftzfs_pool_t *pool,
			swiftzfs_property_list_t **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_dataset_properties(
			const swiftzfs_dataset_t *dataset,
			swiftzfs_property_list_t **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_dataset_property(
			const swiftzfs_dataset_t *dataset,
			const char *name,
			swiftzfs_property_t **result,
			swiftzfs_error_t **error);

	void swiftzfs_property_destroy(swiftzfs_property_t *property);
	const char *swiftzfs_property_name(const swiftzfs_property_t *property);
	const char *swiftzfs_property_value(const swiftzfs_property_t *property);
	swiftzfs_property_source_t swiftzfs_property_source(
			const swiftzfs_property_t *property);
	swiftzfs_property_type_t swiftzfs_property_type(
			const swiftzfs_property_t *property);
	bool swiftzfs_property_readonly(const swiftzfs_property_t *property);

	void swiftzfs_property_list_destroy(swiftzfs_property_list_t *list);
	size_t swiftzfs_property_list_count(const swiftzfs_property_list_t *list);
	const char *swiftzfs_property_list_name_at(
			const swiftzfs_property_list_t *list,
			size_t index);
	const char *swiftzfs_property_list_value_at(
			const swiftzfs_property_list_t *list,
			size_t index);
	swiftzfs_property_source_t swiftzfs_property_list_source_at(
			const swiftzfs_property_list_t *list,
			size_t index);
	swiftzfs_property_type_t swiftzfs_property_list_type_at(
			const swiftzfs_property_list_t *list,
			size_t index);
	bool swiftzfs_property_list_readonly_at(
			const swiftzfs_property_list_t *list,
			size_t index);

	/* Filesystem and volume enumeration. */
	swiftzfs_status_t swiftzfs_pool_datasets(
			const swiftzfs_pool_t *pool,
			swiftzfs_dataset_list_t **result,
			swiftzfs_error_t **error);
	void swiftzfs_dataset_list_destroy(swiftzfs_dataset_list_t *list);
	size_t swiftzfs_dataset_list_count(const swiftzfs_dataset_list_t *list);

	/* Transfer ownership of one dataset out of a list. */
	swiftzfs_dataset_t *swiftzfs_dataset_list_take_at(
			swiftzfs_dataset_list_t *list,
			size_t index);

	void swiftzfs_dataset_destroy(swiftzfs_dataset_t *dataset);
	swiftzfs_dataset_type_t swiftzfs_dataset_type(
			const swiftzfs_dataset_t *dataset);
	swiftzfs_status_t swiftzfs_dataset_copy_name(
			const swiftzfs_dataset_t *dataset,
			char **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_dataset_guid(
			const swiftzfs_dataset_t *dataset,
			uint64_t *result,
			swiftzfs_error_t **error);

	/* Filesystem-only queries. */
	swiftzfs_status_t swiftzfs_filesystem_mounted(
			const swiftzfs_dataset_t *filesystem,
			bool *result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_filesystem_copy_mountpoint(
			const swiftzfs_dataset_t *filesystem,
			char **result,
			swiftzfs_error_t **error);

	/* Volume-only queries. */
	swiftzfs_status_t swiftzfs_volume_size(
			const swiftzfs_dataset_t *volume,
			uint64_t *result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_volume_block_size(
			const swiftzfs_dataset_t *volume,
			uint64_t *result,
			swiftzfs_error_t **error);

	/* Snapshot enumeration and creation. */
	swiftzfs_status_t swiftzfs_dataset_snapshots(
			const swiftzfs_dataset_t *dataset,
			swiftzfs_dataset_list_t **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_dataset_create_snapshot(
			const swiftzfs_dataset_t *dataset,
			const char *snapshot_name,
			const swiftzfs_property_value_t *properties,
			size_t property_count,
			swiftzfs_dataset_t **result,
			swiftzfs_error_t **error);

	/*
	 * Pull-based snapshot send streams.  A stream owns an internal producer and
	 * is consumed by repeatedly calling swiftzfs_send_stream_read() until it
	 * reports zero bytes.  Destroying a partially consumed stream may block while
	 * the remaining producer output is drained.
	 */
	swiftzfs_status_t swiftzfs_snapshot_send_stream(
			const swiftzfs_dataset_t *snapshot,
			const swiftzfs_send_options_t *options,
			swiftzfs_send_stream_t **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_snapshot_send_stream_incremental(
			const swiftzfs_dataset_t *snapshot,
			const swiftzfs_dataset_t *from_snapshot,
			const swiftzfs_send_options_t *options,
			swiftzfs_send_stream_t **result,
			swiftzfs_error_t **error);
	void swiftzfs_send_stream_destroy(swiftzfs_send_stream_t *stream);
	swiftzfs_status_t swiftzfs_send_stream_read(
			swiftzfs_send_stream_t *stream,
			void *buffer,
			size_t capacity,
			size_t *bytes_read,
			swiftzfs_error_t **error);

	/* Direct-to-fd send path.  The C shim never closes fd. */
	swiftzfs_status_t swiftzfs_snapshot_send(
			const swiftzfs_dataset_t *snapshot,
			int fd,
			const swiftzfs_send_options_t *options,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_snapshot_send_incremental(
			const swiftzfs_dataset_t *snapshot,
			const swiftzfs_dataset_t *from_snapshot,
			int fd,
			const swiftzfs_send_options_t *options,
			swiftzfs_error_t **error);

	/* Immediate dataset hierarchy; neither operation recurses. */
	swiftzfs_status_t swiftzfs_pool_children(
			const swiftzfs_pool_t *pool,
			swiftzfs_dataset_list_t **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_dataset_children(
			const swiftzfs_dataset_t *dataset,
			swiftzfs_dataset_list_t **result,
			swiftzfs_error_t **error);

#ifdef __cplusplus
}
#endif

#endif /* SWIFTZFS_H */
