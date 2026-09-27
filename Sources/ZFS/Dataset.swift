import CZFS

/// The common base class for ZFS filesystems, volumes, and snapshots.
public class Dataset {
  let handle: OpaquePointer

  /// Full ZFS dataset name.
  public let name: String

  /// Dataset GUID.
  public let guid: UInt64

  init(handle: OpaquePointer) throws {
    do {
      var pointer: UnsafeMutablePointer<CChar>?
      var error: OpaquePointer?
      let nameStatus = swiftzfs_dataset_copy_name(handle, &pointer, &error)
      try checkSwiftZFSStatus(nameStatus, operation: "dataset.name", error: error)
      guard let name = takeCString(pointer) else {
        throw invariantError("dataset.name", "C shim returned no dataset name")
      }

      var guid: UInt64 = 0
      error = nil
      let guidStatus = swiftzfs_dataset_guid(handle, &guid, &error)
      try checkSwiftZFSStatus(guidStatus, operation: "dataset.guid", error: error)

      self.handle = handle
      self.name = name
      self.guid = guid
    } catch {
      swiftzfs_dataset_destroy(handle)
      throw error
    }
  }

  deinit {
    swiftzfs_dataset_destroy(handle)
  }

  /// Whether this dataset is a filesystem.
  public var isFilesystem: Bool { false }

  /// Whether this dataset is a volume.
  public var isVolume: Bool { false }

  /// Whether this dataset is a snapshot.
  public var isSnapshot: Bool { false }

  /// All native and user-defined properties visible on this dataset.
  public func properties() throws -> [Property] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_dataset_properties(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "dataset.properties", error: error)
    guard let list else {
      throw invariantError("dataset.properties", "C shim returned no property list")
    }
    defer { swiftzfs_property_list_destroy(list) }
    return decodeProperties(from: list)
  }

  /// Return one property by its ZFS property name.
  public func property(named name: String) throws -> Property {
    var property: OpaquePointer?
    var error: OpaquePointer?
    let status = name.withCString {
      swiftzfs_dataset_property(handle, $0, &property, &error)
    }
    try checkSwiftZFSStatus(status, operation: "dataset.property(\(name))", error: error)
    guard let property else {
      throw invariantError("dataset.property(\(name))", "C shim returned no property")
    }
    defer { swiftzfs_property_destroy(property) }

    guard
      let cName = swiftzfs_property_name(property),
      let cValue = swiftzfs_property_value(property)
    else {
      throw invariantError("dataset.property(\(name))", "C shim returned an invalid property")
    }

    return Property(
      name: String(cString: cName),
      value: String(cString: cValue),
      source: propertySource(swiftzfs_property_source(property)),
      valueType: propertyType(swiftzfs_property_type(property)),
      isReadOnly: swiftzfs_property_readonly(property)
    )
  }

  /// Datasets immediately below this dataset.
  ///
  /// This operation is non-recursive. Use `descendants()` for recursive traversal.
  public func children() throws -> [Dataset] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_dataset_children(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "dataset.children", error: error)
    guard let list else {
      throw invariantError("dataset.children", "C shim returned no dataset list")
    }
    defer { swiftzfs_dataset_list_destroy(list) }
    return try Dataset.takeAll(from: list)
  }

  /// All descendants below this dataset, in depth-first pre-order.
  public func descendants() throws -> [Dataset] {
    var result: [Dataset] = []
    for child in try children() {
      result.append(child)
      result.append(contentsOf: try child.descendants())
    }
    return result
  }

  /// Snapshots belonging to this dataset.
  public func snapshots() throws -> [Snapshot] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_dataset_snapshots(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "dataset.snapshots", error: error)
    guard let list else {
      throw invariantError("dataset.snapshots", "C shim returned no snapshot list")
    }
    defer { swiftzfs_dataset_list_destroy(list) }

    let count = swiftzfs_dataset_list_count(list)
    var result: [Snapshot] = []
    result.reserveCapacity(count)

    for index in 0..<count {
      guard let handle = swiftzfs_dataset_list_take_at(list, index) else { continue }
      let dataset = try Dataset.make(handle: handle)
      guard let snapshot = dataset as? Snapshot else {
        throw invariantError("dataset.snapshots", "C shim returned a non-snapshot object")
      }
      result.append(snapshot)
    }

    return result
  }

  /// Look up a snapshot by its name relative to this dataset.
  ///
  /// Pass a name such as `before-upgrade`, without the dataset name or `@`.
  /// Throws `ZFSError` with code `.notFound` if the snapshot does not exist.
  /// Throws `.invalidArgument` for an invalid name or if this is a snapshot.
  public func snapshot(named name: String) throws -> Snapshot {
    guard !name.utf8.contains(0) else {
      throw ZFSError(
        operation: "dataset.snapshot(\(name))",
        code: .invalidArgument,
        systemError: 0,
        message: "snapshot name must not contain a NUL byte"
      )
    }
    var snapshotHandle: OpaquePointer?
    var error: OpaquePointer?
    let status = name.withCString {
      swiftzfs_dataset_snapshot(handle, $0, &snapshotHandle, &error)
    }
    try checkSwiftZFSStatus(status, operation: "dataset.snapshot(\(name))", error: error)
    guard let snapshotHandle else {
      throw invariantError("dataset.snapshot(\(name))", "C shim returned no snapshot")
    }
    return try Snapshot(handle: snapshotHandle)
  }

  static func takeAll(from list: OpaquePointer) throws -> [Dataset] {
    let count = swiftzfs_dataset_list_count(list)
    var result: [Dataset] = []
    result.reserveCapacity(count)

    for index in 0..<count {
      guard let handle = swiftzfs_dataset_list_take_at(list, index) else { continue }
      do {
        result.append(try Dataset.make(handle: handle))
      } catch {
        throw error
      }
    }
    return result
  }

  static func make(handle: OpaquePointer) throws -> Dataset {
    let type = swiftzfs_dataset_type(handle)
    if type == SWIFTZFS_DATASET_FILESYSTEM { return try Filesystem(handle: handle) }
    if type == SWIFTZFS_DATASET_VOLUME { return try Volume(handle: handle) }
    if type == SWIFTZFS_DATASET_SNAPSHOT { return try Snapshot(handle: handle) }

    swiftzfs_dataset_destroy(handle)
    throw invariantError("dataset.type", "C shim returned an unknown dataset type")
  }
}

/// A mountable ZFS filesystem dataset.
public final class Filesystem: Dataset {
  public override var isFilesystem: Bool { true }

  /// Whether the filesystem is currently mounted.
  public var isMounted: Bool {
    get throws {
      var value = false
      var error: OpaquePointer?
      let status = swiftzfs_filesystem_mounted(handle, &value, &error)
      try checkSwiftZFSStatus(status, operation: "filesystem.isMounted", error: error)
      return value
    }
  }

  /// Effective mountpoint reported by ZFS.
  public var mountpoint: String {
    get throws {
      var pointer: UnsafeMutablePointer<CChar>?
      var error: OpaquePointer?
      let status = swiftzfs_filesystem_copy_mountpoint(handle, &pointer, &error)
      try checkSwiftZFSStatus(status, operation: "filesystem.mountpoint", error: error)
      guard let mountpoint = takeCString(pointer) else {
        throw invariantError("filesystem.mountpoint", "C shim returned no mountpoint")
      }
      return mountpoint
    }
  }
}

/// A ZFS volume (zvol) dataset.
public final class Volume: Dataset {
  public override var isVolume: Bool { true }

  /// Logical volume size in bytes.
  public var size: UInt64 {
    get throws {
      var value: UInt64 = 0
      var error: OpaquePointer?
      let status = swiftzfs_volume_size(handle, &value, &error)
      try checkSwiftZFSStatus(status, operation: "volume.size", error: error)
      return value
    }
  }

  /// Volume block size in bytes.
  public var blockSize: UInt64 {
    get throws {
      var value: UInt64 = 0
      var error: OpaquePointer?
      let status = swiftzfs_volume_block_size(handle, &value, &error)
      try checkSwiftZFSStatus(status, operation: "volume.blockSize", error: error)
      return value
    }
  }
}

/// A read-only point-in-time snapshot of a filesystem or volume.
public final class Snapshot: Dataset {
  public override var isSnapshot: Bool { true }

  /// Start a full send stream for this snapshot.
  ///
  /// - Parameter options: Features to include in the stream.
  /// - Returns: A single-consumer stream of data chunks.
  /// - Throws: `ZFSError` if the stream cannot be started.
  public func send(options: SendOptions = SendOptions()) throws -> SendStream {
    var stream: OpaquePointer?
    var error: OpaquePointer?
    var cOptions = options.cValue
    let status = swiftzfs_snapshot_send_stream(handle, &cOptions, &stream, &error)
    try checkSwiftZFSStatus(status, operation: "snapshot.send", error: error)
    guard let stream else {
      throw invariantError("snapshot.send", "C shim returned no send stream")
    }
    return SendStream(handle: stream)
  }

  /// Start an incremental send ending at this snapshot.
  ///
  /// - Parameters:
  ///   - earlier: Earlier snapshot used as the incremental base.
  ///   - options: Features to include in the stream.
  /// - Returns: A single-consumer stream of data chunks.
  /// - Throws: `ZFSError` if the stream cannot be started.
  public func send(from earlier: Snapshot, options: SendOptions = SendOptions()) throws
    -> SendStream
  {
    var stream: OpaquePointer?
    var error: OpaquePointer?
    var cOptions = options.cValue
    let status = swiftzfs_snapshot_send_stream_incremental(
      handle, earlier.handle, &cOptions, &stream, &error)
    try checkSwiftZFSStatus(status, operation: "snapshot.send(from:)", error: error)
    guard let stream else {
      throw invariantError("snapshot.send(from:)", "C shim returned no send stream")
    }
    return SendStream(handle: stream)
  }

  /// Write a full send directly to a caller-owned file descriptor.
  ///
  /// - Parameters:
  ///   - fileDescriptor: Output descriptor, which remains open.
  ///   - options: Features to include in the stream.
  /// - Throws: `ZFSError` if the descriptor is invalid or send fails.
  public func send(toFileDescriptor fileDescriptor: Int32, options: SendOptions = SendOptions())
    throws
  {
    var error: OpaquePointer?
    var cOptions = options.cValue
    let status = swiftzfs_snapshot_send(handle, fileDescriptor, &cOptions, &error)
    try checkSwiftZFSStatus(status, operation: "snapshot.send(toFileDescriptor:)", error: error)
  }

  /// Write an incremental send directly to a caller-owned file descriptor.
  ///
  /// - Parameters:
  ///   - fileDescriptor: Output descriptor, which remains open.
  ///   - earlier: Earlier snapshot used as the incremental base.
  ///   - options: Features to include in the stream.
  /// - Throws: `ZFSError` if the descriptor is invalid or send fails.
  public func send(
    toFileDescriptor fileDescriptor: Int32,
    from earlier: Snapshot,
    options: SendOptions = SendOptions()
  ) throws {
    var error: OpaquePointer?
    var cOptions = options.cValue
    let status = swiftzfs_snapshot_send_incremental(
      handle, earlier.handle, fileDescriptor, &cOptions, &error)
    try checkSwiftZFSStatus(
      status, operation: "snapshot.send(toFileDescriptor:from:)", error: error)
  }

  /// Snapshots cannot themselves contain child datasets.
  public override func children() throws -> [Dataset] { [] }

  /// Snapshots cannot themselves contain descendant datasets.
  public override func descendants() throws -> [Dataset] { [] }

  /// Snapshots cannot themselves contain snapshots.
  public override func snapshots() throws -> [Snapshot] { [] }
}

/// Filesystems and volumes discovered in a single pool traversal.
public struct DatasetCollection {
  /// Filesystem datasets, including the pool root dataset.
  public let filesystems: [Filesystem]

  /// Volume datasets discovered in the same traversal.
  public let volumes: [Volume]

  /// Create a collection from previously discovered datasets.
  ///
  /// - Parameters:
  ///   - filesystems: Filesystem datasets, including any pool root.
  ///   - volumes: Volume datasets.
  public init(filesystems: [Filesystem], volumes: [Volume]) {
    self.filesystems = filesystems
    self.volumes = volumes
  }
}
