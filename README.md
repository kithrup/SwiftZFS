# SwiftZFS C++ skeleton 0.6.0

This revision splits the C++ implementation into multiple translation units while
preserving the public API and the fast dataset enumeration path developed in
0.5.x.

The public C++ API remains in:

    Sources/ZFSCPP/include/ZFS.hpp

OpenZFS-specific types remain private to the implementation.

## C++ source layout

    Sources/ZFSCPP/
        Context.cpp             libzfs context lifetime and setup
        Dataset.cpp             Dataset / Filesystem / Volume implementation
        Error.cpp               zfs::Error implementation
        Pool.cpp                Pool lifetime and basic pool operations
        PoolProperties.cpp      pool properties and feature enumeration
        VDev.cpp                vdev topology parsing
        ZFSManager.cpp          zfs::ZFS and imported-pool enumeration
        internal/ZFSInternal.hpp
        include/ZFS.hpp

`internal/ZFSInternal.hpp` contains the private implementation declarations
shared by the translation units. It is not part of the public API.

## Dataset enumeration

Dataset discovery retains the optimized path from the previous revision:

    zfs_iter_filesystems_v2(..., ZFS_ITER_SIMPLE, ...)

A single traversal classifies filesystem and volume datasets. Full dataset
handles are opened only when details such as GUID, mount state, mountpoint,
volsize, or volblocksize are requested.

## FreeBSD build

For an in-tree configured OpenZFS checkout:

    ./configure --with-zfs-includedir=${HOME}/src/sef-openzfs/include
    gmake clean
    gmake cpp-build
    gmake cpp-run

The generated Makefile currently uses GNU make syntax, so use `gmake` on
FreeBSD.

The C++ smoke program lists imported pools, properties, features, vdevs,
filesystems, and volumes, and retains the timing output for dataset enumeration
so the optimized traversal can be verified on FreeBSD.

## Public design

The current public model includes:

    namespace zfs {
        class ZFS;
        class Pool;
        class ImportablePool;
        class VDev;
        class Dataset;
        class Filesystem;
        class Volume;
        class PoolProperty;
        class PoolFeature;
        class Error;
    }

`Dataset` is the common base for `Filesystem` and `Volume`. Filesystems expose
mount-specific operations, while volumes expose volume-specific properties.

Search/import/create, pool state/status, export/destroy, and several other
operations remain API skeletons for later implementation.
