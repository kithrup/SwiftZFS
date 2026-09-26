# SwiftZFS --- Agent Instructions

## Project Overview

SwiftZFS is a cross-platform Swift interface to OpenZFS.

Primary development platforms: - FreeBSD 16 - Linux - Swift 6

The project is deliberately layered:

``` text
Swift
  |
  v
CZFS — stable C ABI
  |
  v
ZFSCPP — C++17 wrapper
  |
  v
OpenZFS libraries
```

Maintain this dependency direction:

``` text
Swift -> CZFS -> ZFSCPP -> OpenZFS
```

The checked-out working tree is authoritative. Do not reconstruct
current source files from old patches when the working tree is
available.

## Repository Layout

Important source directories:

``` text
Sources/
    ZFS/             Swift interface
    CZFS/            C ABI / Swift bridge
    ZFSCPP/          C++17 OpenZFS wrapper
        include/
            Dataset.hpp
            Error.hpp
            Pool.hpp
            Property.hpp
            VDev.hpp
            ZFS.hpp
            ZFSManager.hpp
        internal/
            ZFSInternal.hpp
    zfs-example/     Swift example/test program
```

`ZFS.hpp` is a lightweight umbrella header.

## Architecture Rules

### Public C++ API

Public C++ headers must not expose OpenZFS headers or implementation
types such as:

``` text
boolean_t
uint_t
uchar_t
nvlist_t
nvpair_t
zfs_type_t
zpool_handle_t
zfs_handle_t
libzfs_handle_t
```

Use standard C++ types and PImpl/internal implementation classes
instead.

### C ABI

The C ABI must expose only Swift-friendly types: - opaque handles -
fixed-width integers - POD enums/structures - C strings - pointer/count
arrays - file descriptors

Never allow C++ exceptions across the C ABI. C entry points must catch
`zfs::Error`, `std::bad_alloc`, `std::exception`, and unknown exceptions
as appropriate and translate them into the CZFS error representation.

### Swift

Swift must not call libzfs directly or import OpenZFS headers.

Do not expose `OpaquePointer` in the public Swift API.

Swift reference types should own their C handles and destroy them in
`deinit`. Convert C strings promptly to Swift `String`, convert C enums
to native Swift enums, and translate CZFS errors into `ZFSError`.

## Object Invariants

Once a valid object exists, properties intrinsic to its identity should
not fail.

For example:

``` swift
pool.name
pool.guid

dataset.name
dataset.guid
dataset.isFilesystem
dataset.isVolume
dataset.isSnapshot
```

must be non-throwing.

General rule:

> If an object cannot validly exist without a value, expose that value
> as a non-throwing property.

If obtaining an invariant can fail, do it while constructing the object.
In Swift, immutable identity information should normally be `let`.

Operations involving new I/O or OpenZFS operations may throw.

## Dataset Model

The hierarchy is:

``` text
Dataset
├── Filesystem
├── Volume
└── Snapshot
```

A clone is not currently a separate class.

Type predicates must be inexpensive and non-throwing.

## Dataset Hierarchy API

The public API distinguishes immediate children from recursive
traversal:

``` swift
try pool.children()
try pool.descendants()

try dataset.children()
try dataset.descendants()
```

Semantics:

``` text
children()      immediate children only
descendants()   all descendants recursively
```

Do not make `children()` silently recursive.

Snapshots are terminal nodes. Volumes currently have no child datasets.

The pool root dataset is not itself returned by `pool.children()`.

For:

``` text
tank
tank/home
tank/home/sef
tank/usr
```

`pool.children()` conceptually returns `tank/home` and `tank/usr`, not
`tank`.

## Dataset Enumeration Performance

Dataset enumeration performance is important.

Previous repeated-traversal implementations took roughly 40 seconds on a
real pool. Lightweight single-pass enumeration reduced this to roughly
0.5 seconds. Do not regress this.

### Single Traversal Requirement

Building a pool dataset hierarchy should require one OpenZFS traversal.

Do not implement recursive hierarchy walking by performing a new libzfs
iterator operation for every `children()` call.

Instead:

1.  enumerate datasets once;
2.  create dataset objects;
3.  establish parent/child relationships;
4.  retain those relationships in the object graph.

After construction, walking the tree should use the cached hierarchy.

The fast enumeration path uses:

``` cpp
zfs_iter_filesystems_v2(..., ZFS_ITER_SIMPLE, ...)
```

where appropriate.

`ZFS_ITER_SIMPLE` is intentional. Open detailed handles lazily when
operations require them.

### Tree Construction

Do not recursively invoke `zfs_iter_filesystems_v2()` from inside a
callback already driven by `zfs_iter_filesystems_v2()`.

A previous implementation did this and produced incorrect relationships,
including a dataset becoming its own descendant.

Preferred strategy:

1.  perform one `zfs_iter_filesystems_v2()` traversal;
2.  collect filesystem/volume objects flat;
3.  build parent/child relationships afterward.

Dataset names can determine parenthood. For example:

``` text
zroot/var/log -> zroot/var
```

Do not rely on iterator callback ordering to infer parenthood. Ensure no
dataset can become its own child.

## DatasetCollection

`DatasetCollection` represents filesystems and volumes discovered during
a single traversal.

Avoid independent filesystem and volume traversals.

Preserve move/ownership optimizations that let the C shim transfer both
vectors from one traversal.

## Snapshots

Snapshots may be enumerated from datasets.

Snapshot creation is represented by `Dataset::create_snapshot(...)` with
optional user properties. Creating a snapshot from a snapshot is
invalid.

## Snapshot Send

Snapshot send is iterable.

C++ should support:

``` cpp
for (const auto& data : snapshot.send()) {
    // consume data
}
```

and incremental send:

``` cpp
for (const auto& data : newer.send(older)) {
    // consume incremental stream
}
```

A direct file-descriptor API may remain:

``` cpp
snapshot.send_to(fd);
newer.send_to(fd, older);
```

The C ABI exposes an opaque send-stream handle and read operation.

The intended Swift API is:

``` swift
for try await data in snapshot.send() {
    // Data
}
```

Do not buffer an entire send stream in memory.

The existing C++ implementation uses a producer thread and pipe around
`lzc_send()`. Be careful with partial-consumption destruction: do not
leave the producer blocked or cause unwanted SIGPIPE behavior.

## OpenZFS Context Lifetime

Returned Pool and Dataset objects may outlive the `ZFS` object that
discovered them.

Objects requiring OpenZFS access must retain the underlying context
appropriately. Use shared ownership internally where necessary.

## FreeBSD

FreeBSD is a primary development platform.

Developer OpenZFS tree may be:

``` text
/home/sef/src/sef-openzfs
```

FreeBSD base OpenZFS source tree:

``` text
/usr/src/sys/contrib/openzfs
```

Installed `/usr/include/libzfs.h` cannot necessarily compile correctly
in isolation because OpenZFS relies on source-tree compatibility
definitions.

For FreeBSD base OpenZFS builds, include setup is approximately:

``` text
-include /usr/src/sys/contrib/openzfs/include/os/freebsd/spl/sys/ccompile.h
-I/usr/src/sys/contrib/openzfs/include
-I/usr/src/sys/contrib/openzfs/module/icp/include
-I/usr/src/sys/contrib/openzfs/lib/libspl/include
-I/usr/src/sys/contrib/openzfs/lib/libspl/include/os/freebsd
-I/usr/src/sys/contrib/openzfs/lib/libzpool/include
```

with:

``` text
-D_GNU_SOURCE
-D_REENTRANT
-D_FILE_OFFSET_BITS=64
-D_LARGEFILE64_SOURCE
```

Do not try to solve compatibility types by arbitrarily including
`sys/stdtypes.h` or `sys/varargs.h`; those approaches were already tried
and are incorrect.

Do not require generated `zfs_config.h` when using the FreeBSD
base-system OpenZFS source tree.

Link installed system libraries as appropriate:

``` text
-lzfs
-lzfs_core
-lnvpair
```

and `-lzpool` only where required.

## Build System

The project uses GNU make functionality. On FreeBSD use `gmake`, not BSD
`make`.

Useful targets include:

``` tcsh
gmake
gmake cpp-build
gmake c-shim-build
gmake c-shim-test
gmake c-shim-run
gmake build
gmake test
gmake docs
gmake clean
```

Swift may also be built with:

``` tcsh
swift build
```

Run the example with:

``` tcsh
swift run zfs-example
```

or locate binaries with:

``` tcsh
swift build --show-bin-path
```

## Shell Environment

The developer uses **tcsh**.

Commands intended to be pasted into the developer's shell must be
tcsh-compatible. Do not assume bash-specific environment assignment,
loops, functions, command substitution, or redirection.

Scripts with an explicit shebang may use another interpreter.

## Formatting

Before producing patches, format modified sources with: - `clang-format`
for C/C++ - `swift-format` for Swift

Preserve project formatting configuration.

Do not claim formatter verification if the formatter is unavailable.

## Compiler Warnings

C/C++ should compile cleanly with strict warnings including:

``` text
-Wall
-Wextra
-Werror
```

Fix underlying warnings rather than suppressing them unless suppression
is demonstrably appropriate.

## Documentation

Public API additions require Doxygen-quality documentation.

Document classes, enums, enum values, methods, parameters, return
values, and exceptions.

Build documentation with:

``` tcsh
gmake docs
```

## Patch Generation

Do not manually fabricate patch hunks.

Generate patches mechanically using `git diff`, `git diff --no-index`,
`diff -u`, or equivalent.

Before delivering a patch validate it with:

``` tcsh
git apply --check --ignore-whitespace patchfile
```

or where appropriate:

``` tcsh
patch --dry-run --ignore-whitespace -p1 < patchfile
```

The developer frequently reformats/reindents files with Emacs. Patches
should tolerate whitespace changes where practical.

Normal application:

``` tcsh
git apply --ignore-whitespace patchfile
```

Avoid unnecessary fragile context.

Before modifying files inspect:

``` tcsh
git status
git diff
```

Preserve unrelated local modifications.

## Testing Changes

Do not stop after compilation when a useful runtime test is available.

For dataset enumeration, test against actual local ZFS pools where safe.

A hierarchy smoke test should produce a structure conceptually like:

``` text
zroot
  zroot/ROOT
    zroot/ROOT/default
  zroot/home
  zroot/usr
    zroot/usr/local
  zroot/var
    zroot/var/audit
    zroot/var/crash
    zroot/var/log
    zroot/var/tmp
```

Check for: - duplicate datasets - wrong parents - self-children -
infinite recursion - missing children

For performance-sensitive enumeration changes, measure elapsed time.
Hierarchy traversal should be near the optimized single-traversal
behavior, not tens of seconds.

## Error Handling

Prefer typed errors with useful operation context.

C++ OpenZFS failures should normally become `zfs::Error`.

The C shim translates those into its stable error representation, and
Swift translates C errors into `ZFSError`.

Do not use fatal termination for normal operational errors.

## Ownership

Be explicit about ownership at ABI boundaries.

The C shim uses ownership-transfer operations such as:

``` text
swiftzfs_pool_list_take_at(...)
swiftzfs_dataset_list_take_at(...)
```

When consuming collections: 1. take ownership of individual objects; 2.
destroy the collection afterward; 3. handle partial conversion failure
without leaks or double destruction.

## Development Approach

Before changing an API or implementation:

1.  inspect the current source;
2.  inspect `git status` and existing local modifications;
3.  identify which layer owns the behavior;
4.  preserve C++ -\> C -\> Swift layering;
5.  consider lifetime and ownership;
6.  avoid unnecessary OpenZFS traversals;
7.  implement the smallest coherent change;
8.  format it;
9.  build with strict warnings;
10. run relevant tests;
11. inspect the resulting diff;
12. mechanically generate and validate requested patches.

Do not reconstruct source files from old patches when the current
working tree is available.

The checked-out working tree is authoritative.

When compiler output contradicts an assumption, inspect the actual
current source and headers before proposing another patch.

## API Design Principles

Prefer APIs that make expensive or surprising behavior explicit.

`children()` means one level.

`descendants()` means recursive traversal.

Prefer clear semantic operations over boolean behavior flags.

Distinguish: - immutable object identity; - cached discovery
information; - operations requiring new OpenZFS I/O.

Do not make inexpensive invariant properties throwing merely because the
underlying C API uses an error-return convention.

## Current Development Direction

The immediate focus is the Swift interface.

Core Swift types:

``` text
ZFS
Pool
Dataset
Filesystem
Volume
Snapshot
Property
ZFSError
DatasetCollection
```

The dataset hierarchy/tree implementation is currently being exercised
and optimized.

Likely subsequent work: 1. snapshot creation in the Swift API; 2. Swift
iterable/asynchronous snapshot send; 3. additional pool/dataset
operations; 4. expanded tests.

Preserve FreeBSD and Linux compatibility.
