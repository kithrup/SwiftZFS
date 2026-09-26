#include <swiftzfs/ZFS.hpp>

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
	if (argc != 2) {
		std::cerr << "Usage: list-snapshots <dataset>\n";
		return 2;
	}

	const std::string dataset_name(argv[1]);
	const std::string pool_name =
		dataset_name.substr(0, dataset_name.find('/'));
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
		const zfs::Dataset *selected = nullptr;
		for (const auto &filesystem : datasets.filesystems()) {
			if (filesystem.name() == dataset_name) {
				selected = &filesystem;
				break;
			}
		}
		if (selected == nullptr) {
			for (const auto &volume : datasets.volumes()) {
				if (volume.name() == dataset_name) {
					selected = &volume;
					break;
				}
			}
		}
		if (selected == nullptr) {
			std::cerr << "Dataset not found: " << dataset_name << '\n';
			return 1;
		}

		for (const auto &snapshot : selected->snapshots())
			std::cout << snapshot.name() << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "list-snapshots: " << error.what() << '\n';
		return 1;
	}
}
