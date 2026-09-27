import CZFS

/// Numeric ZFS pool GUID.
public typealias GUID = UInt64

/// Top-level SwiftZFS context.
public final class ZFS {
  private let handle: OpaquePointer

  public init() throws {
    var context: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_context_create(&context, &error)
    try checkSwiftZFSStatus(status, operation: "ZFS.init", error: error)
    guard let context else {
      throw invariantError("ZFS.init", "C shim returned no context")
    }
    self.handle = context
  }

  deinit {
    swiftzfs_context_destroy(handle)
  }

  /// Enumerate currently imported pools.
  public func pools() throws -> [Pool] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_context_pools(handle, &list, &error)
    try checkSwiftZFSStatus(status, operation: "ZFS.pools", error: error)
    guard let list else {
      throw invariantError("ZFS.pools", "C shim returned no pool list")
    }
    defer { swiftzfs_pool_list_destroy(list) }

    let count = swiftzfs_pool_list_count(list)
    var result: [Pool] = []
    result.reserveCapacity(count)

    for index in 0..<count {
      guard let handle = swiftzfs_pool_list_take_at(list, index) else { continue }
      result.append(try Pool(handle: handle))
    }

    return result
  }

  /// Open a pool that is already imported, by name.
  ///
  /// A pool that is not imported throws `ZFSError` with code `.noSuchPool`.
  public func pool(named name: String) throws -> Pool {
    guard !name.utf8.contains(0) else {
      throw ZFSError(
        operation: "ZFS.pool(\(name))",
        code: .invalidArgument,
        systemError: 0,
        message: "pool name must not contain a NUL byte"
      )
    }
    var poolHandle: OpaquePointer?
    var error: OpaquePointer?
    let status = name.withCString {
      swiftzfs_context_pool(handle, $0, &poolHandle, &error)
    }
    try checkSwiftZFSStatus(status, operation: "ZFS.pool(\(name))", error: error)
    guard let poolHandle else {
      throw invariantError("ZFS.pool(\(name))", "C shim returned no pool")
    }
    return try Pool(handle: poolHandle)
  }

  /// Import a pool by name from attached storage.
  ///
  /// Import does not force access or mount datasets. An already imported pool
  /// throws `.alreadyExists`; a missing importable pool throws `.noSuchPool`.
  public func importPool(named name: String) throws -> Pool {
    guard !name.utf8.contains(0) else {
      throw ZFSError(
        operation: "ZFS.importPool(\(name))",
        code: .invalidArgument,
        systemError: 0,
        message: "pool name must not contain a NUL byte"
      )
    }
    var poolHandle: OpaquePointer?
    var error: OpaquePointer?
    let status = name.withCString {
      swiftzfs_context_import_pool(handle, $0, &poolHandle, &error)
    }
    try checkSwiftZFSStatus(status, operation: "ZFS.importPool(\(name))", error: error)
    guard let poolHandle else {
      throw invariantError("ZFS.importPool(\(name))", "C shim returned no pool")
    }
    return try Pool(handle: poolHandle)
  }

  /// Import a pool by its ASCII GUID in decimal or `0x` hexadecimal form.
  ///
  /// Import does not force access or mount datasets.
  public func importPool(guid: String) throws -> Pool {
    guard !guid.utf8.contains(0) else {
      throw ZFSError(
        operation: "ZFS.importPool(guid: \(guid))",
        code: .invalidArgument,
        systemError: 0,
        message: "pool GUID must not contain a NUL byte"
      )
    }
    var poolHandle: OpaquePointer?
    var error: OpaquePointer?
    let status = guid.withCString {
      swiftzfs_context_import_pool_guid_string(handle, $0, &poolHandle, &error)
    }
    try checkSwiftZFSStatus(status, operation: "ZFS.importPool(guid: \(guid))", error: error)
    guard let poolHandle else {
      throw invariantError("ZFS.importPool(guid: \(guid))", "C shim returned no pool")
    }
    return try Pool(handle: poolHandle)
  }

  /// Import a pool by its numeric GUID.
  ///
  /// Import does not force access or mount datasets.
  public func importPool(guid: GUID) throws -> Pool {
    var poolHandle: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_context_import_pool_guid(handle, guid, &poolHandle, &error)
    try checkSwiftZFSStatus(status, operation: "ZFS.importPool(guid: \(guid))", error: error)
    guard let poolHandle else {
      throw invariantError("ZFS.importPool(guid: \(guid))", "C shim returned no pool")
    }
    return try Pool(handle: poolHandle)
  }

}
