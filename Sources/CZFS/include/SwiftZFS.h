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

	/** Explicit selection for one dataset; all handles and arrays are borrowed. */
	typedef struct swiftzfs_replication_dataset {
		const swiftzfs_dataset_t* dataset;	 /**< Included filesystem or volume. */
		const swiftzfs_dataset_t* from_snapshot; /**< Base or clone origin; NULL for a full send. */
		const swiftzfs_dataset_t* const* snapshots; /**< Selected targets in creation order. */
		size_t snapshot_count; /**< Number of selected targets, excluding the base. */
		const swiftzfs_dataset_t* const*
		    retained_snapshots;		/**< Catalogue-only snapshots already on the receiver. */
		size_t retained_snapshot_count; /**< Number of catalogue-only snapshots. */
	} swiftzfs_replication_dataset_t;

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
	/**
	 * Open a pool that is already imported.
	 * @param context Live ZFS context.
	 * @param name Name of the imported pool.
	 * @param result Receives a caller-owned pool handle on success; destroy it
	 *        with swiftzfs_pool_destroy().
	 * @param error Receives caller-owned error details on failure; destroy them
	 *        with swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success or an error status on failure.
	 */
	swiftzfs_status_t swiftzfs_context_pool(
			swiftzfs_context_t *context,
			const char *name,
			swiftzfs_pool_t **result,
			swiftzfs_error_t **error);
	/**
	 * Import a pool by name from attached devices.
	 *
	 * @param context Live ZFS context.
	 * @param name Pool name to import.
	 * @param result Receives a caller-owned pool handle on success; destroy it
	 *        with swiftzfs_pool_destroy().
	 * @param error Receives caller-owned error details on failure; destroy them
	 *        with swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success or an error status on failure.
	 */
	swiftzfs_status_t swiftzfs_context_import_pool(
			swiftzfs_context_t *context,
			const char *name,
			swiftzfs_pool_t **result,
			swiftzfs_error_t **error);
	/**
	 * Import a pool by an ASCII GUID in decimal or 0x-prefixed hexadecimal.
	 * @param context Live ZFS context.
	 * @param guid_ascii ASCII GUID to import.
	 * @param result Receives a caller-owned pool handle on success; destroy it
	 *        with swiftzfs_pool_destroy().
	 * @param error Receives caller-owned error details on failure; destroy them
	 *        with swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success or an error status on failure.
	 */
	swiftzfs_status_t swiftzfs_context_import_pool_guid_string(
			swiftzfs_context_t *context,
			const char *guid_ascii,
			swiftzfs_pool_t **result,
			swiftzfs_error_t **error);
	/**
	 * Import a pool by its numeric GUID.
	 * @param context Live ZFS context.
	 * @param guid Nonzero pool GUID to import.
	 * @param result Receives a caller-owned pool handle on success; destroy it
	 *        with swiftzfs_pool_destroy().
	 * @param error Receives caller-owned error details on failure; destroy them
	 *        with swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success or an error status on failure.
	 */
	swiftzfs_status_t swiftzfs_context_import_pool_guid(
			swiftzfs_context_t *context,
			uint64_t guid,
			swiftzfs_pool_t **result,
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

	/**
	 * Copy the cached full ZFS path (not an OS mountpoint).
	 * @param dataset Borrowed dataset handle.
	 * @param result Receives an owned string; free with swiftzfs_string_free().
	 * @param error Optional owned error output.
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions escape.
	 */
	swiftzfs_status_t swiftzfs_dataset_copy_path(
		const swiftzfs_dataset_t* dataset, char** result,
		swiftzfs_error_t** error);

	/**
	 * Obtain the cached parent; a snapshot's parent is its source dataset.
	 * @param dataset Borrowed dataset handle.
	 * @param result Receives an owned handle, or NULL for the pool root.
	 * Destroy a non-NULL result with swiftzfs_dataset_destroy(). The parent
	 * remains valid after the input handle is destroyed.
	 * @param error Optional owned error output.
	 * @return SWIFTZFS_OK on success (including root), or an error status.
	 * No exceptions escape.
	 */
	swiftzfs_status_t swiftzfs_dataset_parent(const swiftzfs_dataset_t* dataset,
		swiftzfs_dataset_t** result, swiftzfs_error_t** error);

	/* Pool and dataset properties. */
	/** Creation time in whole seconds since the Unix epoch. Cached with the
	 * GUID on first identity query.
	 * @param dataset Borrowed dataset handle.
	 * @param result Receives the value on success.
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t
	swiftzfs_dataset_creation_time(const swiftzfs_dataset_t *dataset,
								   uint64_t *result, swiftzfs_error_t **error);
	/** Effective compression setting, including any algorithm level. Throws
	 * for snapshots.
	 * @param dataset Borrowed dataset handle.
	 * @param result Receives the value on success. Caller frees the string
	 * with swiftzfs_string_free().
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t
	swiftzfs_dataset_copy_compression(const swiftzfs_dataset_t *dataset,
									  char **result, swiftzfs_error_t **error);
	/** Whether writes are disabled by the readonly property; always true
	 * for snapshots.
	 * @param dataset Borrowed dataset handle.
	 * @param result Receives the value on success.
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t
	swiftzfs_dataset_readonly(const swiftzfs_dataset_t *dataset, bool *result,
							  swiftzfs_error_t **error);
	/** ZFS used bytes, including descendants and reservations; for
	 * snapshots, exclusively referenced bytes.
	 * @param dataset Borrowed dataset handle.
	 * @param result Receives the value on success.
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t
	swiftzfs_dataset_allocated_space(const swiftzfs_dataset_t *dataset,
									 uint64_t *result,
									 swiftzfs_error_t **error);
	/** ZFS available bytes, accounting for quotas and reservations. Throws
	 * for snapshots.
	 * @param dataset Borrowed dataset handle.
	 * @param result Receives the value on success.
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t
	swiftzfs_dataset_free_space(const swiftzfs_dataset_t *dataset,
								uint64_t *result, swiftzfs_error_t **error);
	/** Pool import readonly flag.
	 * @param pool Borrowed pool handle.
	 * @param result Receives the value on success.
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t swiftzfs_pool_readonly(const swiftzfs_pool_t *pool,
											 bool *result,
											 swiftzfs_error_t **error);
	/** Physically allocated pool bytes.
	 * @param pool Borrowed pool handle.
	 * @param result Receives the value on success.
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t swiftzfs_pool_allocated_space(const swiftzfs_pool_t *pool,
													uint64_t *result,
													swiftzfs_error_t **error);
	/** Unallocated pool bytes.
	 * @param pool Borrowed pool handle.
	 * @param result Receives the value on success.
	 * @param error Optional output for an owned error; destroy with
	 * swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. No exceptions
	 * escape.
	 */
	swiftzfs_status_t swiftzfs_pool_free_space(const swiftzfs_pool_t *pool,
											   uint64_t *result,
											   swiftzfs_error_t **error);

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

	/* Snapshot enumeration, lookup, and creation. */
	swiftzfs_status_t swiftzfs_dataset_snapshots(
			const swiftzfs_dataset_t *dataset,
			swiftzfs_dataset_list_t **result,
			swiftzfs_error_t **error);
	/**
	 * Look up a snapshot directly by its relative component name.
	 *
	 * @param dataset Filesystem or volume that owns the snapshot.
	 * @param snapshot_name Name without the dataset prefix or '@' separator.
	 * @param result Receives a dataset handle owned by the caller; destroy it
	 *        with swiftzfs_dataset_destroy().
	 * @param error Receives error details on failure; destroy them with
	 *        swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. A missing snapshot
	 *         reports SWIFTZFS_ERROR_NOT_FOUND in the error object.
	 */
	swiftzfs_status_t swiftzfs_dataset_snapshot(
			const swiftzfs_dataset_t *dataset,
			const char *snapshot_name,
			swiftzfs_dataset_t **result,
			swiftzfs_error_t **error);
	swiftzfs_status_t swiftzfs_dataset_create_snapshot(
			const swiftzfs_dataset_t *dataset,
			const char *snapshot_name,
			const swiftzfs_property_value_t *properties,
			size_t property_count,
			swiftzfs_dataset_t **result,
			swiftzfs_error_t **error);

	/**
	 * Encode an empty-metadata compound BEGIN and its checksummed header END.
	 * Append selected snapshot streams and one swiftzfs_send_compound_footer().
	 * Does not enumerate datasets or generate replication/property metadata.
	 * @param target_snapshot Full final snapshot name, including @.
	 * @param result Receives caller-owned bytes; release with swiftzfs_bytes_free().
	 * @param size Receives the byte count. Set to zero on failure.
	 * @param error Receives owned error details; release with swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. Never throws.
	 */
	swiftzfs_status_t swiftzfs_send_compound_header(const char* target_snapshot, uint8_t** result,
							size_t* size, swiftzfs_error_t** error);

	/**
	 * Encode a compound header including caller-selected replication metadata.
	 * No enumeration is performed; selected objects are queried for properties.
	 * Include the root and all ancestors of selected datasets; supply ordered targets
	 * and optional bases. Encrypted datasets require raw streams. Send parents and
	 * required origin snapshots before dependents. Omitted objects may be removed
	 * by a receiver using -F. Snapshot holds are not included.
	 * @param target_snapshot Final root snapshot, present in the selection.
	 * @param datasets Borrowed array of included datasets and their targets/bases.
	 * @param dataset_count Number of included datasets.
	 * @param options Stream features; NULL selects defaults. Encrypted sends require raw.
	 * @param result Receives owned bytes; release with swiftzfs_bytes_free().
	 * @param size Receives byte count; zero on failure.
	 * @param error Receives owned details; release with swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. Never throws.
	 */
	swiftzfs_status_t swiftzfs_send_replication_header(const swiftzfs_dataset_t* target_snapshot,
							   const swiftzfs_replication_dataset_t* datasets,
							   size_t dataset_count,
							   const swiftzfs_send_options_t* options,
							   uint8_t** result, size_t* size,
							   swiftzfs_error_t** error);

	/**
	 * Encode the final compound END with a zero checksum.
	 * @param result Receives caller-owned bytes; release with swiftzfs_bytes_free().
	 * @param size Receives the byte count. Set to zero on failure.
	 * @param error Receives owned error details; release with swiftzfs_error_destroy().
	 * @return SWIFTZFS_OK on success, or an error status. Never throws.
	 */
	swiftzfs_status_t swiftzfs_send_compound_footer(uint8_t** result, size_t* size,
							swiftzfs_error_t** error);

	/**
	 * Release bytes returned by the compound framing functions.
	 * @param bytes Owned allocation, or NULL. Never throws.
	 */
	void swiftzfs_bytes_free(uint8_t* bytes);

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
	/* Direct lookup by full name, including the pool root and snapshots. */
	swiftzfs_status_t swiftzfs_pool_dataset(
			const swiftzfs_pool_t *pool,
			const char *name,
			swiftzfs_dataset_t **result,
			swiftzfs_error_t **error);
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
