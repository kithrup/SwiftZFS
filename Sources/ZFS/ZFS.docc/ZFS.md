# ``ZFS``

Discover imported ZFS pools and work with their datasets from Swift.

SwiftZFS uses the `ZFS` module for its Swift API. Start with a ``ZFS``
context, open an imported ``Pool`` by name, then look up a dataset by its full
ZFS name:

```swift
import ZFS

let zfs = try ZFS()
let pool = try zfs.pool(named: "tank")
let home = try pool.dataset(named: "tank/home")

print(home.name)
print(home.pool.name) // tank; non-throwing, with no OpenZFS I/O
for child in try home.children() {
  print(child.name)
}
```

Use the pool name itself to open the root filesystem. Names containing `@`
can identify snapshots, such as `tank/home@before-upgrade`. The lookup returns
a ``Dataset``; use `isFilesystem`, `isVolume`, and `isSnapshot` to inspect its
kind, or cast it to ``Filesystem``, ``Volume``, or ``Snapshot`` for type-specific
operations.

Every dataset exposes its containing pool as `dataset.pool`, including root
datasets and snapshots. It retains the original `Pool` instance, keeping pool
operations available even after the discovering `ZFS` context goes away.

Pools and datasets conform to `Hashable` and `Equatable`, so they can be used
as `Set` elements or dictionary keys. Pools compare by pool GUID; datasets
compare by containing pool GUID and dataset GUID. Independently opened wrappers
for the same object compare equal, including `Filesystem`, `Volume`, and
`Snapshot` instances viewed as `Dataset`. Equality and hashing use cached
immutable identity without OpenZFS I/O; names and mutable properties do not
participate. `DatasetCollection` compares its filesystem and volume arrays in
order using this identity. `Property`, `SendOptions`, and `ZFSError` compare
and hash all their stored values.

Lookup and other operations that access OpenZFS throw ``ZFSError``. A missing
dataset has code `ZFSError.Code.notFound`; a name outside the pool has code
`ZFSError.Code.invalidArgument`.

Filesystem and volume lookup constructs the pool hierarchy in one traversal.
The returned dataset retains its immediate children, and `children()` walks
that cached hierarchy. Snapshot lookup opens the named snapshot directly.

## Read common properties

```swift
print(home.creationTime, home.type) // Date and DatasetType; no I/O
print(try home.compression, try home.isReadOnly)
print(try home.allocatedSpace, try home.freeSpace) // UInt64 bytes
print(try pool.allocatedSpace, try pool.freeSpace, try pool.isReadOnly)
if let filesystem = home as? Filesystem {
  print(try filesystem.isMounted, try filesystem.mountpoint)
}
```

Dataset `allocatedSpace` is ZFS `used`: it includes descendants and reservations,
and a snapshot reports only exclusively referenced space. Dataset `freeSpace`
is ZFS `available`, accounting for quotas and reservations. Pool values are
physical `allocated` and `free` bytes; pool free space is not a promise of
writable dataset capacity. Pool `isReadOnly` reports its import flag.

Snapshots are always read-only; their compression and free-space queries throw
`ZFSError` because those native properties do not apply. Mount state and
mountpoint belong to `Filesystem`. Mountpoint preserves ZFS values such as
`none` and `legacy`. Compression is a string so algorithm levels and future
OpenZFS algorithms can be represented without losing information.

Pools have no native creation, dataset type, mountpoint, or compression property.
Open the pool's root dataset with `pool.dataset(named: pool.name)` to query
those dataset values. Its creation time describes that dataset.

C++ provides `Dataset::type()`, `creation_time()` (Unix seconds), `compression()`,
`readonly()`, `allocated_space()`, and `free_space()`. Pool exposes `readonly()`,
`allocated_space()`, and `free_space()`. Dataset identity is loaded lazily and
cached together, preserving lightweight enumeration; Swift loads identity during
construction and exposes immutable `let` properties. Mutable getters query
OpenZFS and can fail. C++ creation-time access can throw on its first query.

## Create a snapshot

Create a snapshot of a filesystem or volume, optionally setting user properties:

```swift
let snapshot = try home.createSnapshot(
  named: "before-upgrade",
  properties: ["org.example:purpose": "Before system upgrade"]
)
print(snapshot.name) // tank/home@before-upgrade
```

Pass only the relative snapshot name. Creation is non-recursive and requires
ZFS snapshot permissions, plus user-property permissions when setting properties.
Omit `properties` when none are needed. Creating a snapshot from another snapshot
is invalid. Failures, including an existing snapshot name, throw `ZFSError`.
The snapshot persists on disk after the Swift object is released.

## Send a snapshot

Look up a snapshot and consume its send stream in bounded chunks:

```swift
let dataset = try pool.dataset(named: "tank/home")
let snapshot = try dataset.snapshot(named: "before-upgrade")
for try await data in try snapshot.send() {
  // Write each Data chunk to your destination.
}
```

Use `try newer.send(from: older)` for an incremental stream. Passing `nil` as
the base is equivalent to `send()` and produces a full stream. The
file-descriptor overload also accepts a nil base for a full send. `SendOptions`
controls embedded data, large blocks, compression, and raw encrypted sends.
For an existing output file descriptor, `try snapshot.send(toFileDescriptor: fd)`
writes directly without a Swift read loop. The caller keeps ownership of `fd`.

A send stream has one consumer. Releasing it before the end may block while
OpenZFS finishes producing and the stream is drained.

## Topics

### Discover pools

- ``ZFS``
- ``Pool``

### Work with datasets

- ``Dataset``
- ``Filesystem``
- ``Volume``
- ``Snapshot``
- ``DatasetCollection``
- ``DatasetType``
- ``SendStream``
- ``SendOptions``

### Values and errors

- ``Property``
- ``ZFSError``
