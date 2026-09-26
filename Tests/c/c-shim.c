#include "SwiftZFS.h"

#include <stdio.h>

	static void
print_error(const char *operation, swiftzfs_error_t *error)
{
	fprintf(stderr, "%s: %s", operation,
			error != NULL ? swiftzfs_error_message(error) : "unknown error");
	if (error != NULL && swiftzfs_error_system_error(error) != 0)
		fprintf(stderr, " (system error %d)",
				swiftzfs_error_system_error(error));
	fputc('\n', stderr);
}

	int
main(void)
{
	swiftzfs_context_t *context = NULL;
	swiftzfs_pool_list_t *pools = NULL;
	swiftzfs_error_t *error = NULL;

	if (swiftzfs_context_create(&context, &error) != SWIFTZFS_OK) {
		print_error("swiftzfs_context_create", error);
		swiftzfs_error_destroy(error);
		return 1;
	}

	if (swiftzfs_context_pools(context, &pools, &error) != SWIFTZFS_OK) {
		print_error("swiftzfs_context_pools", error);
		swiftzfs_error_destroy(error);
		swiftzfs_context_destroy(context);
		return 1;
	}

	for (size_t i = 0; i < swiftzfs_pool_list_count(pools); ++i) {
		swiftzfs_pool_t *pool = swiftzfs_pool_list_take_at(pools, i);
		char *name = NULL;

		if (pool == NULL)
			continue;

		if (swiftzfs_pool_copy_name(pool, &name, &error) != SWIFTZFS_OK) {
			print_error("swiftzfs_pool_copy_name", error);
			swiftzfs_error_destroy(error);
			swiftzfs_pool_destroy(pool);
			swiftzfs_pool_list_destroy(pools);
			swiftzfs_context_destroy(context);
			return 1;
		}

		printf("%s\n", name);
		swiftzfs_string_free(name);
		swiftzfs_pool_destroy(pool);
	}

	swiftzfs_pool_list_destroy(pools);
	swiftzfs_context_destroy(context);
	return 0;
}
