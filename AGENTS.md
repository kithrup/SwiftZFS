# Agent guidance for SwiftZFS

## User requirements

- Do not create, edit, delete, or otherwise change any file without the user's express permission. Permission for one requested file does not authorize edits to other files.
- The user uses `tcsh`. Run shell commands with `tcsh`, and write shell examples in `tcsh` syntax.
- Before running builds, tests, formatters, generators, or cleanup commands that may change files, obtain express permission for those changes. Read-only inspection is allowed.
- Preserve existing uncommitted and untracked work. Do not treat generated or untracked files as disposable.

## Project overview

SwiftZFS is a Swift Package Manager package wrapping OpenZFS. `README.md` and `configure.ac` call this version 0.6.1, although the checkout directory is named `SwiftZFS-0.6.0`. The package uses Swift tools 6.0 and C++17. Its products are the `ZFS` Swift library and `zfs-example` executable.

The layers are:

1. `Sources/ZFSCPP/include/`: public C++ API, with `ZFS.hpp` as the umbrella header.
2. `Sources/ZFSCPP/*.cpp` and `internal/ZFSInternal.hpp`: implementation and private OpenZFS details.
3. `Sources/CZFS/include/SwiftZFS.h` and `SwiftZFS.cpp`: C ABI with opaque handles, status/error results, and explicit destroy/free functions.
4. `Sources/ZFS/*.swift`: Swift wrappers and value types over the C ABI.

The C++ API has `zfs::ZFS`, `Pool`, `VDev`, `Dataset`, `Filesystem`, `Volume`, `Snapshot`, properties, errors, and send streams. It enumerates imported pools, pool metadata and vdevs, datasets, and snapshots. Snapshot creation and full/incremental sends exist in the C++ and C layers. The Swift layer currently exposes pool and dataset inspection but does not yet expose snapshot creation or sending.

Bulk C++ dataset discovery uses `zfs_iter_filesystems_v2(..., ZFS_ITER_SIMPLE, ...)` and opens full dataset handles lazily for detail queries. Current uncommitted C++ changes build an in-memory parent/child hierarchy during that traversal. Snapshot enumeration uses `zfs_iter_snapshots_v2`.

`ZFS::userspace_version`, `kernel_version`, `search_pools`, `import_pool`, and `create_pool` remain `not_implemented` placeholders. So do `Pool::state`, `status`, `export_pool`, and `destroy`.

## Build and checks

`configure.ac` locates OpenZFS headers and libraries and generates `Makefile` from `Makefile.in`. It supports `--with-zfs-includedir`, `--with-zfs-libdir`, `--with-zfs-source`, and `--with-zfs-build`. FreeBSD can use `/usr/src/sys/contrib/openzfs` headers with base-system libraries. The generated Makefile uses GNU make syntax; use `gmake` on FreeBSD. Main targets include `cpp-build`, `cpp-test`, `cpp-run`, `c-shim-build`, `c-shim-test`, `c-shim-run`, `build`, `release`, `test`, `run`, and `docs`. The default target builds the C++ and C smoke programs. The Swift manifest receives OpenZFS compile/link flags through `SWIFTZFS_CXXFLAGS` and `SWIFTZFS_LINKER_FLAGS`.

`Tests/ZFSTests/ZFSTests.swift` has small value/error tests. `Tests/cpp/list-pools.cpp` lists imported pools, properties, features, vdevs, filesystems, and volumes and reports dataset timing. `Tests/c/c-shim.c` exercises C context and pool enumeration. `Doxyfile` generates C++ API documentation under `docs/`.

## Working tree at creation

Before this file was created, `Sources/ZFSCPP/Dataset.cpp`, `Sources/ZFSCPP/internal/ZFSInternal.hpp`, and `Sources/zfs-example/main.swift` were modified. `.build-cpp/`, `configure~`, and `docs/` were untracked. Do not overwrite or clean them without express permission. No build or tests were run while preparing this file.
