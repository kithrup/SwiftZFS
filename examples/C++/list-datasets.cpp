#include <swiftzfs/ZFS.hpp>

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
	if (argc != 2) {
		std::cerr << "Usage: list-datasets <pool>\n";
		return 2;
	}

	const std::string pool_name(argv[1]);
	try {
		zfs::ZFS zfs;
		auto pools = zfs.pools();
		const auto pool =
			std::find_if(pools.begin(), pools.end(), [&](const auto &value) {
				return value.name() == pool_name;
			});
		if (pool == pools.end()) {
			std::cerr << "Pool not found: " << pool_name << '\n';
			return 1;
		}

		auto datasets = pool->datasets();
		for (const auto &filesystem : datasets.filesystems())
			std::cout << filesystem.name() << '\n';
		for (const auto &volume : datasets.volumes())
			std::cout << volume.name() << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "list-datasets: " << error.what() << '\n';
		return 1;
	}
}
