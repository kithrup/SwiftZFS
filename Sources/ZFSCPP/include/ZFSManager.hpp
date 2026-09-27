#pragma once

#include "Pool.hpp"

#include <memory>
#include <string>
#include <vector>

namespace zfs {
	/** Numeric pool GUID carried without OpenZFS implementation types. */
	using guid_t = std::uint64_t;

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
			 * Open a pool that is already imported.
			 * @param name Pool name.
			 * @return A live pool wrapper.
			 * @throws zfs::Error with Error::Code::no_such_pool if not imported.
			 * @throws zfs::Error if the name is invalid or opening fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			Pool pool(const std::string& name) const;

			/**
			 * Search attached storage for pools that can potentially be imported.
			 *
			 * @return Discovered import candidates when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			std::vector<ImportablePool> search_pools() const;

			/**
			 * Import a pool by name from attached storage.
			 *
			 * Import is not forced and does not mount datasets.
			 *
			 * @param name Pool name to import.
			 * @return A live Pool wrapper for the imported pool.
			 * @throws zfs::Error with Error::Code::invalid_argument if name is invalid.
			 * @throws zfs::Error with Error::Code::already_exists if already imported.
			 * @throws zfs::Error with Error::Code::no_such_pool if no pool matches.
			 * @throws zfs::Error if discovery, import, or opening the pool fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			Pool import_pool(const std::string& name);

			/**
			 * Import a pool by its ASCII GUID (decimal or 0x-prefixed hexadecimal).
			 * @param guid_ascii ASCII GUID to import.
			 * @return A live Pool wrapper for the imported pool.
			 * @throws zfs::Error with Error::Code::invalid_argument if GUID is invalid.
			 * @throws zfs::Error with Error::Code::already_exists if already imported.
			 * @throws zfs::Error with Error::Code::no_such_pool if no pool matches.
			 * @throws zfs::Error if discovery, import, or opening fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			Pool import_pool_guid(const std::string& guid_ascii);

			/**
			 * Import a pool by its numeric GUID.
			 * @param guid Nonzero pool GUID to import.
			 * @return A live Pool wrapper for the imported pool.
			 * @throws zfs::Error with Error::Code::invalid_argument if GUID is zero.
			 * @throws zfs::Error with Error::Code::already_exists if already imported.
			 * @throws zfs::Error with Error::Code::no_such_pool if no pool matches.
			 * @throws zfs::Error if discovery, import, or opening fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			Pool import_pool(guid_t guid);

			/**
			 * Import a previously discovered pool.
			 *
			 * @param pool Import candidate returned by search_pools().
			 * @return A live Pool wrapper for the imported pool.
			 * @throws zfs::Error if import fails.
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
			Pool import_pool_discovered(const std::string& target, guid_t guid);
			class Impl;
			std::unique_ptr<Impl> impl_;
	};

} // namespace zfs
