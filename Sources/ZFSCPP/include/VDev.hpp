#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zfs {

	/** Functional role of a vdev within a pool. */
	enum class VDevRole {
		data,    ///< Normal data-storage vdev.
		log,     ///< Separate intent log (SLOG) vdev.
		spare,   ///< Hot spare vdev.
		cache,   ///< L2ARC cache vdev.
		special, ///< Special allocation-class vdev.
		dedup,   ///< Deduplication allocation-class vdev.
		unknown  ///< Role could not be determined.
	};

	/**
	 * Read-only description of a vdev in a pool topology.
	 *
	 * VDev is a value type.  Composite vdevs such as mirrors and RAID-Z groups
	 * contain child VDev objects.  Removed top-level ZFS "hole" placeholders are
	 * intentionally omitted from the public topology.
	 */
	class VDev {
		public:
			/**
			 * Return the OpenZFS vdev type name (for example "disk", "mirror", or
			 * "raidz").
			 *
			 * @return Reference to the type string owned by this VDev.
			 * @throws Nothing.
			 */
			const std::string& type() const noexcept;

			/**
			 * Return the device path, if the vdev has one.
			 *
			 * @return Reference to the path string; empty for vdevs without a path.
			 * @throws Nothing.
			 */
			const std::string& path() const noexcept;

			/**
			 * Return the vdev GUID.
			 *
			 * @return The vdev GUID, or zero when the OpenZFS configuration has none.
			 * @throws Nothing.
			 */
			std::uint64_t guid() const noexcept;

			/**
			 * Return the vdev's functional role in the pool.
			 *
			 * @return The inherited or explicitly configured vdev role.
			 * @throws Nothing.
			 */
			VDevRole role() const noexcept;

			/**
			 * Return this vdev's immediate children.
			 *
			 * @return Reference to the child-vdev collection owned by this object.
			 * @throws Nothing.
			 */
			const std::vector<VDev>& children() const noexcept;

			/**
			 * Test whether this vdev has no child vdevs.
			 *
			 * @return true for a leaf vdev; false for a composite vdev.
			 * @throws Nothing.
			 */
			bool leaf() const noexcept;

			/**
			 * Construct a vdev description.
			 *
			 * Primarily intended for wrapper implementation code and tests.
			 *
			 * @param type OpenZFS vdev type name.
			 * @param path Device path, or an empty string if not applicable.
			 * @param guid Vdev GUID.
			 * @param role Functional role in the pool.
			 * @param children Immediate child vdevs.
			 * @throws std::bad_alloc if owned strings or child storage cannot be allocated.
			 */
			VDev(std::string type, std::string path, std::uint64_t guid,
					VDevRole role, std::vector<VDev> children = {});

		private:
			std::string type_;
			std::string path_;
			std::uint64_t guid_ = 0;
			VDevRole role_ = VDevRole::unknown;
			std::vector<VDev> children_;
	};

	/**
	 * Convert a VDevRole to a stable printable name.
	 * @param role Vdev role to convert.
	 * @return Pointer to a static null-terminated string.
	 * @throws Nothing.
	 */
	const char* to_string(VDevRole role) noexcept;

} // namespace zfs
