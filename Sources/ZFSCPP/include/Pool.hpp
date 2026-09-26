#pragma once

#include "Dataset.hpp"
#include "Property.hpp"
#include "VDev.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zfs {

	/**
	 * Administrative state recorded for a ZFS storage pool.
	 *
	 * This describes how the pool is recorded/discovered, not its health.  Use
	 * PoolStatus for health such as online or degraded.
	 */
	enum class PoolState {
		unknown,            ///< State could not be determined.
		active,             ///< Pool appears to be currently imported/active.
		exported,           ///< Pool was cleanly exported and is available for import.
		destroyed,          ///< Pool was destroyed but can still be discovered in metadata.
		potentially_active, ///< Pool appears active elsewhere or was not cleanly exported.
		unavailable         ///< Pool state cannot presently be used or determined reliably.
	};

	/** High-level operational health of a ZFS storage pool. */
	enum class PoolStatus {
		unknown,     ///< Health could not be determined.
		online,      ///< Pool is operating normally.
		degraded,    ///< Pool is operating with reduced redundancy or another fault.
		faulted,     ///< Pool cannot operate because of unrecoverable faults.
		offline,     ///< Pool or required devices are administratively offline.
		unavailable, ///< Pool cannot currently be accessed.
		removed      ///< Pool or required devices have been removed.
	};

	/** State reported for an OpenZFS pool feature. */
	enum class FeatureState {
		disabled,             ///< Feature is supported but has not been enabled.
		enabled,              ///< Feature is enabled but has no active on-disk use.
		active,               ///< Feature is enabled and actively used by on-disk data.
		unsupported_inactive, ///< Unknown feature is present but not currently active.
		unsupported_readonly, ///< Unknown feature requires read-only pool access.
		unknown               ///< Feature state could not be interpreted.
	};

	/** Read-only description of a pool feature flag. */
	class PoolFeature {
		public:
			/**
			 * Return the feature's short name, without the "feature@" prefix.
			 *
			 * @return Reference to the feature name owned by this object.
			 * @throws Nothing.
			 */
			const std::string& name() const noexcept;

			/**
			 * Return the feature's current state.
			 *
			 * @return The feature state.
			 * @throws Nothing.
			 */
			FeatureState state() const noexcept;

			/**
			 * Test whether this feature is recognized by the running OpenZFS library.
			 *
			 * @return true for supported features; false for unsupported feature entries.
			 * @throws Nothing.
			 */
			bool supported() const noexcept;

		private:
			friend class Pool;

			PoolFeature(std::string name, FeatureState state, bool supported);

			std::string name_;
			FeatureState state_ = FeatureState::unknown;
			bool supported_ = true;
	};

	/**
	 * Wrapper for an imported/live ZFS storage pool.
	 *
	 * Pool owns a live zpool handle and exposes pool metadata, feature flags, vdev
	 * topology, and the filesystems/volumes contained in the pool.  Pool objects
	 * are move-only because the underlying zpool handle has unique ownership.
	 */
	class Pool {
		public:
			/**
			 * Move-construct a pool wrapper.
			 * @param other Pool whose zpool-handle ownership is transferred.
			 * @throws Nothing.
			 */
			Pool(Pool&& other) noexcept;

			/**
			 * Move-assign a pool wrapper.
			 * @param other Pool whose zpool-handle ownership is transferred.
			 * @return Reference to this object.
			 * @throws Nothing.
			 */
			Pool& operator=(Pool&& other) noexcept;

			/** Pool wrappers are non-copyable because they uniquely own a zpool handle. */
			Pool(const Pool&) = delete;

			/** Pool wrappers are non-copy-assignable because they uniquely own a zpool handle. */
			Pool& operator=(const Pool&) = delete;

			/**
			 * Close the underlying zpool handle, if any.
			 * @throws Nothing.
			 */
			~Pool();

			/**
			 * Return the pool name.
			 *
			 * @return The imported pool's name.
			 * @throws std::bad_alloc if copying the name fails.
			 */
			std::string name() const;

			/**
			 * Return the pool GUID.
			 *
			 * @return The pool's GUID property.
			 * @throws Nothing from the wrapper; libzfs is queried directly.
			 */
			std::uint64_t guid() const;

			/**
			 * Return all native pool properties.
			 *
			 * Feature properties are returned separately by features().
			 *
			 * @return Native pool properties and their current values.
			 * @throws zfs::Error if property-list construction or querying fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			std::vector<Property> properties() const;

			/**
			 * Return feature@ and unsupported@ entries associated with the pool.
			 *
			 * @return Pool feature descriptions.
			 * @throws zfs::Error if feature enumeration or querying fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			std::vector<PoolFeature> features() const;

			/**
			 * Return the pool's public vdev topology.
			 *
			 * OpenZFS "hole" placeholders are omitted; missing vdevs remain visible.
			 *
			 * @return Top-level data, log, special, dedup, spare, and cache vdevs.
			 * @throws zfs::Error if the pool configuration cannot be retrieved or is
			 *         malformed.
			 * @throws std::bad_alloc if topology storage cannot be allocated.
			 */
			std::vector<VDev> vdevs() const;

			/**
			 * Enumerate filesystems and volumes in one dataset-tree traversal.
			 *
			 * @return A collection containing both dataset categories.
			 * @throws zfs::Error if dataset enumeration fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			DatasetCollection datasets() const;

			/**
			 * Convenience wrapper returning only filesystem datasets.
			 *
			 * @return Filesystems discovered in the pool.
			 * @throws zfs::Error if dataset enumeration fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			std::vector<Filesystem> filesystems() const;

			/**
			 * Convenience wrapper returning only volume datasets.
			 *
			 * @return Volumes discovered in the pool.
			 * @throws zfs::Error if dataset enumeration fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			std::vector<Volume> volumes() const;

			/**
			 * Return the pool state.
			 *
			 * @return Pool state when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			PoolState state() const;

			/**
			 * Return the pool health/status.
			 *
			 * @return Pool status when implemented.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			PoolStatus status() const;

			/**
			 * Export this imported pool from the system.
			 *
			 * @return Nothing.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			void export_pool();

			/**
			 * Destroy this imported pool.
			 *
			 * @return Nothing.
			 * @throws zfs::Error with Error::Code::not_implemented in the current
			 *         implementation.
			 */
			void destroy();

		private:
			friend class ZFS;

			class Impl;
			explicit Pool(std::unique_ptr<Impl> impl) noexcept;

			std::unique_ptr<Impl> impl_;
	};

	/**
	 * Description of a pool discovered on storage but not currently imported.
	 *
	 * ImportablePool is a copyable value object because discovery data is detached
	 * from live zpool handles.
	 */
	class ImportablePool {
		public:
			/** Copy-construct a detached import candidate. @param other Source candidate. */
			ImportablePool(const ImportablePool& other) = default;

			/** Copy-assign a detached import candidate. @param other Source candidate. @return *this. */
			ImportablePool& operator=(const ImportablePool& other) = default;

			/** Move-construct an import candidate. @param other Source candidate. @throws Nothing. */
			ImportablePool(ImportablePool&& other) noexcept = default;

			/** Move-assign an import candidate. @param other Source candidate. @return *this. @throws Nothing. */
			ImportablePool& operator=(ImportablePool&& other) noexcept = default;

			/** Destroy the detached discovery data. @throws Nothing. */
			~ImportablePool() = default;

			/**
			 * Return the name recorded in the discovered pool configuration.
			 * @return Reference to the pool name owned by this object.
			 * @throws Nothing.
			 */
			const std::string& name() const noexcept;

			/**
			 * Return the GUID recorded in the discovered pool configuration.
			 * @return The discovered pool GUID.
			 * @throws Nothing.
			 */
			std::uint64_t guid() const noexcept;

			/**
			 * Return the administrative state found during discovery.
			 * @return The discovered PoolState.
			 * @throws Nothing.
			 */
			PoolState state() const noexcept;

			/**
			 * Return the vdev topology recovered during import discovery.
			 * @return Reference to the topology owned by this object.
			 * @throws Nothing.
			 */
			const std::vector<VDev>& vdevs() const noexcept;

		private:
			friend class ZFS;

			ImportablePool(std::string name, std::uint64_t guid, PoolState state,
					std::vector<VDev> vdevs = {});

			std::string name_;
			std::uint64_t guid_ = 0;
			PoolState state_ = PoolState::unknown;
			std::vector<VDev> vdevs_;
	};


	/**
	 * Convert a FeatureState to a stable printable name.
	 * @param state Feature state to convert.
	 * @return Pointer to a static null-terminated string.
	 * @throws Nothing.
	 */
	const char* to_string(FeatureState state) noexcept;

} // namespace zfs
