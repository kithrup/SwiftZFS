import CZFS

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

}
