#include "ZFS.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void check_parent(const zfs::Dataset& dataset)
{
    require(dataset.path() == dataset.name(), "incorrect dataset path");
    const auto separator = dataset.path().find('@') != std::string::npos
        ? dataset.path().find('@')
        : dataset.path().rfind('/');
    auto parent = dataset.parent();
    if (separator == std::string::npos) {
        require(parent == nullptr, "root has a parent");
        return;
    }
    require(parent != nullptr, "missing parent");
    require(parent->path() == dataset.path().substr(0, separator), "wrong parent");
}
}

int main()
{
    try {
        std::unique_ptr<zfs::Dataset> retained;
        std::unique_ptr<zfs::Dataset> retained_snapshot;
        std::size_t count = 0;
        {
            zfs::ZFS context;
            for (const auto& pool : context.pools()) {
                const auto start = std::chrono::steady_clock::now();
                auto datasets = pool.datasets();
                std::unordered_set<std::string> names;
                auto check = [&](const zfs::Dataset& dataset) {
                    check_parent(dataset);
                    require(names.insert(dataset.path()).second, "duplicate dataset");
                    auto children = dataset.children();
                    auto check_child = [&](const zfs::Dataset& child) {
                        check_parent(child);
                        require(child.parent()->path() == dataset.path(), "wrong child relationship");
                    };
                    for (const auto& child : children.filesystems())
                        check_child(child);
                    for (const auto& child : children.volumes())
                        check_child(child);
                    ++count;
                };
                for (const auto& dataset : datasets.filesystems())
                    check(dataset);
                for (const auto& dataset : datasets.volumes())
                    check(dataset);
                std::cout << pool.name() << ": " << names.size() << " datasets in "
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count()
                          << " ms\n";
                if (!datasets.filesystems().empty())
                    retained = pool.dataset(datasets.filesystems().back().name());
                // Exercise enumeration, relative lookup, and full-name lookup.
                for (const auto& dataset : datasets.filesystems()) {
                    auto snapshots = dataset.snapshots();
                    if (snapshots.empty())
                        continue;
                    for (const auto& snapshot : snapshots)
                        check_parent(snapshot);
                    const auto& snapshot = snapshots.front();
                    auto relative = dataset.snapshot(snapshot.path().substr(snapshot.path().find('@') + 1));
                    check_parent(relative);
                    retained_snapshot = pool.dataset(snapshot.path());
                    break;
                }
            }
        }
        // The discovering context, pool, and all collections have been released.
        if (retained) {
            check_parent(*retained);
            std::unordered_set<std::string> ancestors;
            while (retained) {
                require(ancestors.insert(retained->path()).second, "parent cycle");
                retained = retained->parent();
            }
        }
        if (retained_snapshot) {
            check_parent(*retained_snapshot);
            auto source = retained_snapshot->parent();
            auto children = source->children();
            for (const auto& child : children.filesystems())
                check_parent(child);
        }
        std::cout << "Checked " << count << " datasets; snapshot lifetime check: "
                  << (retained_snapshot ? "passed" : "no snapshots available") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
