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
for child in try home.children() {
  print(child.name)
}
```

Use the pool name itself to open the root filesystem. Names containing `@`
can identify snapshots, such as `tank/home@before-upgrade`. The lookup returns
a ``Dataset``; use `isFilesystem`, `isVolume`, and `isSnapshot` to inspect its
kind, or cast it to ``Filesystem``, ``Volume``, or ``Snapshot`` for type-specific
operations.

Lookup and other operations that access OpenZFS throw ``ZFSError``. A missing
dataset has code `ZFSError.Code.notFound`; a name outside the pool has code
`ZFSError.Code.invalidArgument`.

Filesystem and volume lookup constructs the pool hierarchy in one traversal.
The returned dataset retains its immediate children, and `children()` walks
that cached hierarchy. Snapshot lookup opens the named snapshot directly.

## Send a snapshot

Look up a snapshot and consume its send stream in bounded chunks:

```swift
let dataset = try pool.dataset(named: "tank/home")
let snapshot = try dataset.snapshot(named: "before-upgrade")
for try await data in try snapshot.send() {
  // Write each Data chunk to your destination.
}
```

Use `try newer.send(from: older)` for an incremental stream. `SendOptions`
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
- ``SendStream``
- ``SendOptions``

### Values and errors

- ``Property``
- ``ZFSError``
