#include <swiftzfs/ZFS.hpp>

#include <exception>
#include <iostream>

int main(int argc, char *[])
{
	if (argc != 1) {
		std::cerr << "Usage: list-pools\n";
		return 2;
	}

	try {
		zfs::ZFS zfs;
		for (const auto &pool : zfs.pools())
			std::cout << pool.name() << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "list-pools: " << error.what() << '\n';
		return 1;
	}
}
