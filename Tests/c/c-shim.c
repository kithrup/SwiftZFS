#include "SwiftZFS.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
print_error(const char* operation, swiftzfs_error_t* error)
{
	fprintf(stderr, "%s: %s", operation,
			error != NULL ? swiftzfs_error_message(error) : "unknown error");
	if (error != NULL && swiftzfs_error_system_error(error) != 0)
		fprintf(stderr, " (system error %d)",
				swiftzfs_error_system_error(error));
	fputc('\n', stderr);
}

static void
test_compound_framing(void)
{
	/* Framing is independent of a live OpenZFS context. */
	uint8_t* framing = NULL;
	size_t framing_size = 0;
	swiftzfs_error_t* framing_error = NULL;
	assert(swiftzfs_send_compound_header("tank/home@target", &framing, &framing_size, &framing_error) ==
	       SWIFTZFS_OK);
	assert(framing != NULL && framing_size > 0 && framing_error == NULL);
	size_t header_size = framing_size;
	swiftzfs_bytes_free(framing);
	assert(swiftzfs_send_compound_footer(&framing, &framing_size, &framing_error) == SWIFTZFS_OK);
	assert(framing != NULL && header_size == 2 * framing_size);
	swiftzfs_bytes_free(framing);
	assert(swiftzfs_send_compound_header("tank/home", &framing, &framing_size, &framing_error) !=
	       SWIFTZFS_OK);
	assert(framing == NULL && framing_size == 0 && framing_error != NULL);
	swiftzfs_error_destroy(framing_error);
	assert(swiftzfs_send_compound_header(NULL, &framing, &framing_size, &framing_error) ==
	       SWIFTZFS_INVALID_ARGUMENT);
	swiftzfs_error_destroy(framing_error);
	assert(swiftzfs_send_compound_footer(NULL, &framing_size, &framing_error) ==
	       SWIFTZFS_INVALID_ARGUMENT);
	swiftzfs_error_destroy(framing_error);
	assert(swiftzfs_send_replication_header(NULL, NULL, 0, NULL, &framing, &framing_size,
						&framing_error) == SWIFTZFS_INVALID_ARGUMENT);
	assert(framing == NULL && framing_size == 0);
	swiftzfs_error_destroy(framing_error);
	swiftzfs_bytes_free(NULL);
}

	int
main(void)
{
	test_compound_framing();
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

		swiftzfs_dataset_list_t* datasets = NULL;
		assert(swiftzfs_pool_datasets(pool, &datasets, &error) == SWIFTZFS_OK);
		for (size_t j = 0; j < swiftzfs_dataset_list_count(datasets); ++j)
		{
			swiftzfs_dataset_t* dataset = swiftzfs_dataset_list_take_at(datasets, j);
			swiftzfs_dataset_t* parent = NULL;
			char* path = NULL;
			assert(swiftzfs_dataset_copy_path(dataset, &path, &error) == SWIFTZFS_OK);
			assert(swiftzfs_dataset_parent(dataset, &parent, &error) == SWIFTZFS_OK);
			swiftzfs_dataset_destroy(dataset);
			const char* slash = strrchr(path, '/');
			if (slash == NULL)
			{
				assert(parent == NULL);
				assert(strcmp(path, name) == 0);
			}
			else
			{
				char* parent_path = NULL;
				assert(parent != NULL);
				assert(swiftzfs_dataset_copy_path(parent, &parent_path, &error) == SWIFTZFS_OK);
				assert(strlen(parent_path) == (size_t)(slash - path));
				assert(strncmp(parent_path, path, strlen(parent_path)) == 0);
				swiftzfs_string_free(parent_path);
			}
			swiftzfs_dataset_destroy(parent);
			swiftzfs_string_free(path);
		}
		swiftzfs_dataset_list_destroy(datasets);

		printf("%s\n", name);
		swiftzfs_string_free(name);
		swiftzfs_pool_destroy(pool);
	}

	swiftzfs_pool_list_destroy(pools);
	swiftzfs_context_destroy(context);
	return 0;
}
