import CZFS
import Foundation

/// The concrete kind of a ZFS dataset.
public enum DatasetType: Hashable, Sendable {
  /// A mountable filesystem.
  case filesystem
  /// A block volume.
  case volume
  /// A point-in-time snapshot.
  case snapshot
}

// Scoped to one conversion so shared ancestors are decoded only once.
final class DatasetDiscovery {
  let pool: Pool
  var datasets: [String: Dataset] = [:]

  init(pool: Pool) {
    self.pool = pool
  }

  convenience init(parent: Dataset) {
    self.init(pool: parent.pool)
    datasets[parent.name] = parent
  }
}

/// The common base class for ZFS filesystems, volumes, and snapshots.
///
/// Equality and hashing use the cached containing pool GUID and dataset GUID.
/// Independently opened wrappers compare equal regardless of cached names or
/// properties. Comparisons perform no OpenZFS I/O. All subclasses inherit this
/// identity, including when used through the base class.
public class Dataset: Hashable {
  let handle: OpaquePointer

  /// Full ZFS dataset name.
  public let name: String

  /// The pool containing this dataset, including for the root dataset and snapshots.
  ///
  /// Retains the original pool wrapper for this dataset's lifetime. Access is
  /// non-throwing and performs no OpenZFS I/O.
  public let pool: Pool

  /// Full ZFS path, including the pool name and any `@snapshot` suffix.
  /// This is the dataset name, rather than an operating-system mountpoint.
  public var path: String { name }

  /// Immediate parent, or `nil` for the pool root dataset.
  /// A snapshot's parent is the filesystem or volume it snapshots.
  /// Retained so the parent remains available for this object's lifetime.
  public let parent: Dataset?

  /// Dataset GUID.
  public let guid: UInt64

  /// Immutable creation time recorded when this wrapper is constructed.
  public let creationTime: Date

  /// Concrete dataset kind, recorded during discovery.
  public let type: DatasetType

  /// Compare dataset identity within its containing pool.
  ///
  /// - Parameters:
  ///   - lhs: First dataset wrapper.
  ///   - rhs: Second dataset wrapper.
  /// - Returns: Whether both pool GUIDs and dataset GUIDs match.
  public static func == (lhs: Dataset, rhs: Dataset) -> Bool {
    lhs.pool == rhs.pool && lhs.guid == rhs.guid
  }

  /// Hash the immutable dataset identity without OpenZFS I/O.
  ///
  /// - Parameter hasher: Hasher to receive the pool and dataset GUIDs.
  public final func hash(into hasher: inout Hasher) {
    hasher.combine(pool)
    hasher.combine(guid)
  }

  init(handle: OpaquePointer, discovery: DatasetDiscovery) throws {
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

      var creation: UInt64 = 0
      error = nil
      let creationStatus = swiftzfs_dataset_creation_time(handle, &creation, &error)
      try checkSwiftZFSStatus(creationStatus, operation: "dataset.creationTime", error: error)
      switch swiftzfs_dataset_type(handle) {
      case SWIFTZFS_DATASET_FILESYSTEM: self.type = .filesystem
      case SWIFTZFS_DATASET_VOLUME: self.type = .volume
      case SWIFTZFS_DATASET_SNAPSHOT: self.type = .snapshot
      default: throw invariantError("dataset.type", "C shim returned an unknown dataset type")
      }
      var parentHandle: OpaquePointer?
      error = nil
      let parentStatus = swiftzfs_dataset_parent(handle, &parentHandle, &error)
      try checkSwiftZFSStatus(parentStatus, operation: "dataset.parent", error: error)
      self.parent = try parentHandle.map { try Dataset.make(handle: $0, discovery: discovery) }
      self.creationTime = Date(timeIntervalSince1970: TimeInterval(creation))
      self.handle = handle
      self.name = name
      self.pool = discovery.pool
      self.guid = guid
    } catch {
      swiftzfs_dataset_destroy(handle)
      throw error
    }
  }

  /// Whether the dataset is read-only. Always true for snapshots.
  ///
  /// - Throws: `ZFSError` if OpenZFS cannot query the value.
  public var isReadOnly: Bool {
    get throws {
      var value: Bool = false
      var error: OpaquePointer?
      let status = swiftzfs_dataset_readonly(handle, &value, &error)
      try checkSwiftZFSStatus(status, operation: "dataset.isReadOnly", error: error)
      return value
    }
  }

  /// Allocated bytes. ZFS used includes descendants and reservations; snapshots report exclusively referenced bytes.
  ///
  /// - Throws: `ZFSError` if OpenZFS cannot query the value.
  public var allocatedSpace: UInt64 {
    get throws {
      var value: UInt64 = 0
      var error: OpaquePointer?
      let status = swiftzfs_dataset_allocated_space(handle, &value, &error)
      try checkSwiftZFSStatus(status, operation: "dataset.allocatedSpace", error: error)
      return value
    }
  }

  /// Available bytes after quotas and reservations. Throws `ZFSError` for snapshots.
  ///
  /// - Throws: `ZFSError` if OpenZFS cannot query the value.
  public var freeSpace: UInt64 {
    get throws {
      var value: UInt64 = 0
      var error: OpaquePointer?
      let status = swiftzfs_dataset_free_space(handle, &value, &error)
      try checkSwiftZFSStatus(status, operation: "dataset.freeSpace", error: error)
      return value
    }
  }

  /// Effective compression setting, including any algorithm level. Throws `ZFSError` for snapshots.
  ///
  /// - Throws: `ZFSError` if OpenZFS cannot query the value.
  public var compression: String {
    get throws {
      var value: UnsafeMutablePointer<CChar>?
      var error: OpaquePointer?
      let status = swiftzfs_dataset_copy_compression(handle, &value, &error)
      try checkSwiftZFSStatus(status, operation: "dataset.compression", error: error)
      guard let value = takeCString(value) else {
        throw invariantError("dataset.compression", "C shim returned no value")
      }
      return value
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
    return try Dataset.takeAll(from: list, discovery: DatasetDiscovery(parent: self))
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

  /// Snapshots belonging to this dataset, oldest first by creation transaction.
  public func snapshots() throws -> [Snapshot] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_dataset_snapshots(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "dataset.snapshots", error: error)
    guard let list else {
      throw invariantError("dataset.snapshots", "C shim returned no snapshot list")
    }
    defer { swiftzfs_dataset_list_destroy(list) }

    let discovery = DatasetDiscovery(parent: self)
    let count = swiftzfs_dataset_list_count(list)
    var result: [Snapshot] = []
    result.reserveCapacity(count)

    for index in 0..<count {
      guard let handle = swiftzfs_dataset_list_take_at(list, index) else { continue }
      let dataset = try Dataset.make(handle: handle, discovery: discovery)
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
    return try Snapshot(handle: snapshotHandle, discovery: DatasetDiscovery(parent: self))
  }

  /// Create a snapshot of this filesystem or volume, without snapshotting descendants.
  ///
  /// - Parameters:
  ///   - name: Snapshot name relative to this dataset, such as `before-upgrade`,
  ///     without the dataset name or `@`.
  ///   - properties: Optional ZFS user properties, such as `org.example:purpose`.
  /// - Returns: The newly created snapshot, which retains its OpenZFS context.
  /// - Throws: `ZFSError` for invalid input, an existing snapshot, insufficient
  ///   permissions, or another OpenZFS failure. Snapshots cannot create snapshots.
  ///
  /// Creating the on-disk snapshot can succeed even if constructing its Swift
  /// wrapper subsequently fails. Releasing the returned object does not delete it.
  public func createSnapshot(named name: String, properties: [String: String] = [:]) throws
    -> Snapshot
  {
    let operation = "dataset.createSnapshot(\(name))"
    guard !name.isEmpty, !name.contains("/"), !name.contains("@") else {
      throw ZFSError(
        operation: operation,
        code: .invalidArgument,
        systemError: 0,
        message: "snapshot name must be a nonempty relative name without '/' or '@'"
      )
    }
    guard !name.utf8.contains(0),
      !properties.contains(where: { $0.key.utf8.contains(0) || $0.value.utf8.contains(0) })
    else {
      throw ZFSError(
        operation: operation,
        code: .invalidArgument,
        systemError: 0,
        message: "snapshot name and properties must not contain NUL bytes"
      )
    }

    // Store all property strings together so their pointers remain valid for the C call.
    var strings: [CChar] = []
    var offsets: [(name: Int, value: Int)] = []
    for (key, value) in properties {
      let keyOffset = strings.count
      strings.append(contentsOf: key.utf8CString)
      let valueOffset = strings.count
      strings.append(contentsOf: value.utf8CString)
      offsets.append((keyOffset, valueOffset))
    }

    var snapshotHandle: OpaquePointer?
    var error: OpaquePointer?
    let status = strings.withUnsafeBufferPointer { buffer in
      let values = offsets.map { offset in
        swiftzfs_property_value_t(
          name: buffer.baseAddress!.advanced(by: offset.name),
          value: buffer.baseAddress!.advanced(by: offset.value))
      }
      return values.withUnsafeBufferPointer { values in
        name.withCString {
          swiftzfs_dataset_create_snapshot(
            handle, $0, values.baseAddress, values.count, &snapshotHandle, &error)
        }
      }
    }
    try checkSwiftZFSStatus(status, operation: operation, error: error)
    guard let snapshotHandle else {
      throw invariantError(operation, "C shim returned no snapshot")
    }
    return try Snapshot(handle: snapshotHandle, discovery: DatasetDiscovery(parent: self))
  }

  static func takeAll(
    from list: OpaquePointer, discovery: DatasetDiscovery
  ) throws -> [Dataset] {
    let count = swiftzfs_dataset_list_count(list)
    var result: [Dataset] = []
    result.reserveCapacity(count)

    for index in 0..<count {
      guard let handle = swiftzfs_dataset_list_take_at(list, index) else { continue }
      do {
        result.append(try Dataset.make(handle: handle, discovery: discovery))
      } catch {
        throw error
      }
    }
    return result
  }

  static func make(
    handle: OpaquePointer, discovery: DatasetDiscovery
  ) throws -> Dataset {
    var pointer: UnsafeMutablePointer<CChar>?
    var error: OpaquePointer?
    let status = swiftzfs_dataset_copy_name(handle, &pointer, &error)
    do {
      try checkSwiftZFSStatus(status, operation: "dataset.name", error: error)
    } catch {
      swiftzfs_dataset_destroy(handle)
      throw error
    }
    if let name = takeCString(pointer), let existing = discovery.datasets[name] {
      swiftzfs_dataset_destroy(handle)
      return existing
    }

    let dataset: Dataset
    switch swiftzfs_dataset_type(handle) {
    case SWIFTZFS_DATASET_FILESYSTEM:
      dataset = try Filesystem(handle: handle, discovery: discovery)
    case SWIFTZFS_DATASET_VOLUME:
      dataset = try Volume(handle: handle, discovery: discovery)
    case SWIFTZFS_DATASET_SNAPSHOT:
      dataset = try Snapshot(handle: handle, discovery: discovery)
    default:
      swiftzfs_dataset_destroy(handle)
      throw invariantError("dataset.type", "C shim returned an unknown dataset type")
    }
    discovery.datasets[dataset.name] = dataset
    return dataset
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
    return SendStream(handle: stream, snapshot: self, from: nil)
  }

  /// Start a full or incremental send ending at this snapshot.
  ///
  /// - Parameters:
  ///   - earlier: Earlier snapshot used as the incremental base, or `nil` for a full send.
  ///   - options: Features to include in the stream.
  /// - Returns: A single-consumer stream of data chunks.
  /// - Throws: `ZFSError` if the stream cannot be started.
  public func send(from earlier: Snapshot?, options: SendOptions = SendOptions()) throws
    -> SendStream
  {
    guard let earlier else { return try send(options: options) }
    var stream: OpaquePointer?
    var error: OpaquePointer?
    var cOptions = options.cValue
    let status = swiftzfs_snapshot_send_stream_incremental(
      handle, earlier.handle, &cOptions, &stream, &error)
    try checkSwiftZFSStatus(status, operation: "snapshot.send(from:)", error: error)
    guard let stream else {
      throw invariantError("snapshot.send(from:)", "C shim returned no send stream")
    }
    return SendStream(handle: stream, snapshot: self, from: earlier)
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

  /// Write a full or incremental send directly to a caller-owned file descriptor.
  ///
  /// - Parameters:
  ///   - fileDescriptor: Output descriptor, which remains open.
  ///   - earlier: Earlier snapshot used as the incremental base, or `nil` for a full send.
  ///   - options: Features to include in the stream.
  /// - Throws: `ZFSError` if the descriptor is invalid or send fails.
  public func send(
    toFileDescriptor fileDescriptor: Int32,
    from earlier: Snapshot?,
    options: SendOptions = SendOptions()
  ) throws {
    guard let earlier else {
      return try send(toFileDescriptor: fileDescriptor, options: options)
    }
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
///
/// Equality and hashing include both arrays in order, using dataset identity.
public struct DatasetCollection: Hashable {
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
