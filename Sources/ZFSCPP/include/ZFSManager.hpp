#pragma once

#include "Pool.hpp"

#include <memory>
#include <string>
#include <vector>

namespace zfs {

	/**
	 * Top-level OpenZFS library context.
	 *
	 * ZFS owns the libzfs context used for pool discovery and management.  Objects
	 * returned by this class retain shared internal context as necessary so their
	 * lifetime is not tied to the lifetime of the originating ZFS object.
	 */
	class ZFS {
		public:
			/**
			 * Initialize libzfs.
			 *
			 * @throws zfs::Error with Error::Code::initialization_failed if libzfs
			 *         initialization fails.
			 */
			ZFS();

			/** Finalize the owned libzfs context after dependent shared state is gone. */
			~ZFS();

			/** ZFS contexts are non-copyable. */
			ZFS(const ZFS&) = delete;

			/** ZFS contexts are non-copy-assignable. */
			ZFS& operator=(const ZFS&) = delete;

			/**
			 * Move-construct a ZFS context wrapper.
			 * @param other Context wrapper whose implementation is transferred.
			 * @throws Nothing.
			 */
			ZFS(ZFS&& other) noexcept;

			/**
			 * Move-assign a ZFS context wrapper.
			 * @param other Context wrapper whose implementation is transferred.
			 * @return Reference to this object.
			 * @throws Nothing.
			 */
			ZFS& operator=(ZFS&& other) noexcept;

			/**
			 * Return the OpenZFS userspace version.
			 *
			 * @return Userspace OpenZFS version string when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			std::string userspace_version() const;

			/**
			 * Return the running kernel OpenZFS version.
			 *
			 * @return Kernel OpenZFS version string when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			std::string kernel_version() const;

			/**
			 * Enumerate currently imported pools.
			 *
			 * @return Live Pool wrappers for every imported pool visible to libzfs.
			 * @throws zfs::Error if libzfs pool iteration fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			std::vector<Pool> pools() const;

			/**
			 * Search attached storage for pools that can potentially be imported.
			 *
			 * @return Discovered import candidates when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			std::vector<ImportablePool> search_pools() const;

			/**
			 * Import a previously discovered pool.
			 *
			 * @param pool Import candidate returned by search_pools().
			 * @return A live Pool wrapper for the imported pool when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			Pool import_pool(const ImportablePool& pool);

			/**
			 * Create a new pool.
			 *
			 * The vdev/configuration API is still to be designed; this placeholder
			 * currently accepts only a pool name.
			 *
			 * @param name Name for the new pool.
			 * @return A live Pool wrapper for the new pool when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			Pool create_pool(const std::string& name);

		private:
			class Impl;
			std::unique_ptr<Impl> impl_;
	};

} // namespace zfs
