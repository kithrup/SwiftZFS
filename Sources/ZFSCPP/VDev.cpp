#include "internal/ZFSInternal.hpp"

#include <libnvpair.h>
#include <sys/fs/zfs.h>

#include <cstring>
#include <utility>

namespace zfs {

	VDev::VDev(std::string type, std::string path, std::uint64_t guid,
			VDevRole role, std::vector<VDev> children)
		: type_(std::move(type)), path_(std::move(path)), guid_(guid),
		role_(role), children_(std::move(children))
	{
	}

	const std::string&
		VDev::type() const noexcept
		{
			return type_;
		}

	const std::string&
		VDev::path() const noexcept
		{
			return path_;
		}

	std::uint64_t
		VDev::guid() const noexcept
		{
			return guid_;
		}

	VDevRole
		VDev::role() const noexcept
		{
			return role_;
		}

	const std::vector<VDev>&
		VDev::children() const noexcept
		{
			return children_;
		}

	bool
		VDev::leaf() const noexcept
		{
			return children_.empty();
		}

	const char*
		to_string(VDevRole role) noexcept
		{
			switch (role) {
				case VDevRole::data: return "data";
				case VDevRole::log: return "log";
				case VDevRole::spare: return "spare";
				case VDevRole::cache: return "cache";
				case VDevRole::special: return "special";
				case VDevRole::dedup: return "dedup";
				case VDevRole::unknown: return "unknown";
			}
			return "unknown";
		}

	namespace {

		VDevRole
			vdev_role(nvlist_t* vdev, VDevRole inherited) noexcept
			{
				if (inherited == VDevRole::spare || inherited == VDevRole::cache)
					return inherited;

				uint64_t is_log = 0;
				if (nvlist_lookup_uint64(vdev, ZPOOL_CONFIG_IS_LOG, &is_log) == 0 &&
						is_log != 0) {
					return VDevRole::log;
				}

				const char* bias = nullptr;
				if (nvlist_lookup_string(vdev, ZPOOL_CONFIG_ALLOCATION_BIAS, &bias) == 0 &&
						bias != nullptr) {
					if (std::strcmp(bias, "special") == 0)
						return VDevRole::special;
					if (std::strcmp(bias, "dedup") == 0)
						return VDevRole::dedup;
					if (std::strcmp(bias, "log") == 0)
						return VDevRole::log;
				}

				return inherited == VDevRole::unknown ? VDevRole::data : inherited;
			}

		/*
		 * A "hole" is an internal placeholder left behind for a removed
		 * top-level vdev so vdev IDs remain stable.  It is not a usable device
		 * and is therefore omitted from the public topology.  VDEV_TYPE_MISSING
		 * is intentionally not filtered because a missing vdev is meaningful
		 * pool state that callers need to see.
		 */
		bool
			vdev_is_hole(nvlist_t* vdev)
			{
				const char* type = nullptr;

				return nvlist_lookup_string(vdev, ZPOOL_CONFIG_TYPE, &type) == 0 &&
					type != nullptr && std::strcmp(type, "hole") == 0;
			}

		VDev
			make_vdev(nvlist_t* vdev, VDevRole inherited)
			{
				const char* type = nullptr;
				const char* path = nullptr;
				uint64_t guid = 0;

				(void)nvlist_lookup_string(vdev, ZPOOL_CONFIG_TYPE, &type);
				(void)nvlist_lookup_string(vdev, ZPOOL_CONFIG_PATH, &path);
				(void)nvlist_lookup_uint64(vdev, ZPOOL_CONFIG_GUID, &guid);

				const VDevRole role = vdev_role(vdev, inherited);
				std::vector<VDev> children;

				nvlist_t** child_array = nullptr;
				uint_t child_count = 0;
				if (nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_CHILDREN,
							&child_array, &child_count) == 0) {
					children.reserve(child_count);
					for (uint_t i = 0; i < child_count; ++i) {
						if (!vdev_is_hole(child_array[i]))
							children.push_back(make_vdev(child_array[i], role));
					}
				}

				return VDev(type != nullptr ? type : "unknown",
						path != nullptr ? path : "", guid, role, std::move(children));
			}

		void
			append_vdev_array(nvlist_t* root, const char* key, VDevRole role,
					std::vector<VDev>& result)
			{
				nvlist_t** array = nullptr;
				uint_t count = 0;
				if (nvlist_lookup_nvlist_array(root, key, &array, &count) != 0)
					return;

				result.reserve(result.size() + count);
				for (uint_t i = 0; i < count; ++i) {
					if (!vdev_is_hole(array[i]))
						result.push_back(make_vdev(array[i], role));
				}
			}

	} // namespace

	std::vector<VDev>
		Pool::vdevs() const
		{
			nvlist_t* config = zpool_get_config(impl_->handle(), nullptr);
			if (config == nullptr)
				detail::throw_libzfs_error(*impl_->context(), "zpool_get_config()");

			nvlist_t* root = nullptr;
			if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &root) != 0 ||
					root == nullptr) {
				throw Error(Error::Code::invalid_pool, 0,
						"pool configuration does not contain a vdev tree");
			}

			std::vector<VDev> result;

			nvlist_t** children = nullptr;
			uint_t child_count = 0;
			if (nvlist_lookup_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
						&children, &child_count) == 0) {
				result.reserve(child_count);
				for (uint_t i = 0; i < child_count; ++i) {
					if (!vdev_is_hole(children[i]))
						result.push_back(make_vdev(children[i], VDevRole::data));
				}
			}

			append_vdev_array(root, ZPOOL_CONFIG_SPARES, VDevRole::spare, result);
			append_vdev_array(root, ZPOOL_CONFIG_L2CACHE, VDevRole::cache, result);

			return result;
		}

} // namespace zfs
