#ifndef SWIFTZFS_H
#define SWIFTZFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public C ABI for the Swift wrapper.
 *
 * Deliberately contains no OpenZFS headers or OpenZFS-specific types.
 */

typedef struct swiftzfs_context swiftzfs_context_t;
typedef struct swiftzfs_pool_list swiftzfs_pool_list_t;

enum {
    SWIFTZFS_OK = 0,
    SWIFTZFS_ERROR = -1,
    SWIFTZFS_NO_MEMORY = -2,
    SWIFTZFS_INVALID_ARGUMENT = -3
};

/* Library context. */
int32_t swiftzfs_context_create(swiftzfs_context_t **result);
void swiftzfs_context_destroy(swiftzfs_context_t *context);

/*
 * Error information belongs to the context and remains valid until the next
 * operation on that context or until the context is destroyed.
 */
int32_t swiftzfs_context_last_zfs_error(const swiftzfs_context_t *context);
const char *swiftzfs_context_last_error_description(
    const swiftzfs_context_t *context);

/* Snapshot of currently visible pool names. */
int32_t swiftzfs_pool_list_create(
    swiftzfs_context_t *context,
    swiftzfs_pool_list_t **result);
void swiftzfs_pool_list_destroy(swiftzfs_pool_list_t *list);
size_t swiftzfs_pool_list_count(const swiftzfs_pool_list_t *list);
const char *swiftzfs_pool_list_name_at(
    const swiftzfs_pool_list_t *list,
    size_t index);

#ifdef __cplusplus
}
#endif

#endif /* SWIFTZFS_H */
