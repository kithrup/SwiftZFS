import CZFS

/// A currently imported ZFS storage pool.
public final class Pool {
  private let handle: OpaquePointer

  /// Pool name.
  public let name: String

  /// Pool GUID.
  public let guid: UInt64

  init(handle: OpaquePointer) throws {
    do {
      var pointer: UnsafeMutablePointer<CChar>?
      var error: OpaquePointer?
      let nameStatus = swiftzfs_pool_copy_name(handle, &pointer, &error)
      try checkSwiftZFSStatus(nameStatus, operation: "pool.name", error: error)
      guard let name = takeCString(pointer) else {
        throw invariantError("pool.name", "C shim returned no pool name")
      }

      var guid: UInt64 = 0
      error = nil
      let guidStatus = swiftzfs_pool_guid(handle, &guid, &error)
      try checkSwiftZFSStatus(guidStatus, operation: "pool.guid", error: error)

      self.handle = handle
      self.name = name
      self.guid = guid
    } catch {
      swiftzfs_pool_destroy(handle)
      throw error
    }
  }

  deinit {
    swiftzfs_pool_destroy(handle)
  }

  /// Properties reported for this pool.
  public func properties() throws -> [Property] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_pool_properties(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "pool.properties", error: error)
    guard let list else {
      throw invariantError("pool.properties", "C shim returned no property list")
    }
    defer { swiftzfs_property_list_destroy(list) }
    return decodeProperties(from: list)
  }

  /// Datasets immediately below the pool root dataset.
  ///
  /// The pool root dataset itself is not included. This operation is non-recursive.
  public func children() throws -> [Dataset] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_pool_children(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "pool.children", error: error)
    guard let list else {
      throw invariantError("pool.children", "C shim returned no dataset list")
    }
    defer { swiftzfs_dataset_list_destroy(list) }
    return try Dataset.takeAll(from: list)
  }

  /// All descendants below the pool root dataset, in depth-first pre-order.
  public func descendants() throws -> [Dataset] {
    var result: [Dataset] = []
    for child in try children() {
      result.append(child)
      result.append(contentsOf: try child.descendants())
    }
    return result
  }

  /// Enumerate filesystems and volumes in one traversal of the pool.
  public func datasets() throws -> DatasetCollection {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_pool_datasets(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "pool.datasets", error: error)
    guard let list else {
      throw invariantError("pool.datasets", "C shim returned no dataset list")
    }
    defer { swiftzfs_dataset_list_destroy(list) }

    let count = swiftzfs_dataset_list_count(list)
    var filesystems: [Filesystem] = []
    var volumes: [Volume] = []

    for index in 0..<count {
      guard let handle = swiftzfs_dataset_list_take_at(list, index) else { continue }
      let dataset = try Dataset.make(handle: handle)
      if let filesystem = dataset as? Filesystem {
        filesystems.append(filesystem)
      } else if let volume = dataset as? Volume {
        volumes.append(volume)
      } else {
        throw invariantError("pool.datasets", "C shim returned an unexpected snapshot")
      }
    }

    return DatasetCollection(filesystems: filesystems, volumes: volumes)
  }
}
