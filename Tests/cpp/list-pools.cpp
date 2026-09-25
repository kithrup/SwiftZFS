#include "ZFS.hpp"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>


namespace {

void
print_vdev(const zfs::VDev& vdev, unsigned int depth)
{
    const std::string indent(depth * 2, ' ');

    std::cout << indent << zfs::to_string(vdev.role()) << ": "
              << vdev.type();
    if (!vdev.path().empty())
        std::cout << " " << vdev.path();
    std::cout << " [0x" << std::hex << vdev.guid() << std::dec << "]\n";

    for (const auto& child : vdev.children())
        print_vdev(child, depth + 1);
}

} // namespace

int
main()
{
    try {
        zfs::ZFS zfs;
        auto pools = zfs.pools();

        for (const auto& pool : pools) {
            std::cout << pool.name()
                      << "\t0x"
                      << std::hex << pool.guid() << std::dec
                      << '\n';

            std::cout << "  properties:\n";
            for (const auto& property : pool.properties()) {
                std::cout << "    " << property.name()
                          << " = " << property.value()
                          << " [" << zfs::to_string(property.type())
                          << ", " << zfs::to_string(property.source());
                if (property.readonly())
                    std::cout << ", readonly";
                std::cout << "]\n";
            }

            std::cout << "  features:\n";
            for (const auto& feature : pool.features()) {
                std::cout << "    "
                          << (feature.supported() ? "feature@" : "unsupported@")
                          << feature.name()
                          << " = " << zfs::to_string(feature.state())
                          << '\n';
            }

            std::cout << "  vdevs:\n";
            for (const auto& vdev : pool.vdevs())
                print_vdev(vdev, 2);

            const auto datasets_start = std::chrono::steady_clock::now();
            auto datasets = pool.datasets();
            const auto datasets_stop = std::chrono::steady_clock::now();
            std::cerr << "[timing] " << pool.name() << " datasets(): "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             datasets_stop - datasets_start)
                             .count()
                      << " ms\n";

            std::cout << "  filesystems:\n";
            const auto filesystem_details_start = std::chrono::steady_clock::now();
	    bool show_snapshots = true;

            for (const auto& filesystem : datasets.filesystems()) {
                std::cout << "    " << filesystem.name()
                          << " [0x" << std::hex << filesystem.guid() << std::dec << "]"
                          << " mounted=" << (filesystem.mounted() ? "yes" : "no")
                          << " mountpoint=" << filesystem.mountpoint()
                          << '\n';
		if (show_snapshots) {
			for (const auto& snapshot: filesystem.snapshots()) {
				std::cout << "        " << snapshot.name()
					   << std::endl;
			}
			show_snapshots = false;
		}
            }
            const auto filesystem_details_stop = std::chrono::steady_clock::now();
            std::cerr << "[timing] " << pool.name()
                      << " filesystem details/output: "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             filesystem_details_stop - filesystem_details_start)
                             .count()
                      << " ms\n";

            std::cout << "  volumes:\n";
            const auto volume_details_start = std::chrono::steady_clock::now();
            for (const auto& volume : datasets.volumes()) {
                std::cout << "    " << volume.name()
                          << " [0x" << std::hex << volume.guid() << std::dec << "]"
                          << " size=" << volume.size()
                          << " block_size=" << volume.block_size()
                          << '\n';
            }
            const auto volume_details_stop = std::chrono::steady_clock::now();
            std::cerr << "[timing] " << pool.name()
                      << " volume details/output: "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             volume_details_stop - volume_details_start)
                             .count()
                      << " ms\n";
        }

        return 0;
    } catch (const zfs::Error& error) {
        std::cerr << "ZFS error: " << error.what();
        if (error.system_error() != 0)
            std::cerr << " (code " << error.system_error() << ')';
        std::cerr << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
