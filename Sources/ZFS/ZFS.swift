import CZFS

public struct ZFSError: Error, CustomStringConvertible, Sendable {
  public let operation: String
  public let code: swiftzfs_error_code_t
  public let systemError: Int32
  public let message: String

  public var description: String {
    if systemError != 0 {
      return "\(operation): \(message) (system error \(systemError))"
    }
    return "\(operation): \(message)"
  }
}

public struct ZPool: Hashable, Sendable {
  public let name: String

  public init(name: String) {
    self.name = name
  }
}

public final class ZFS {
  private let handle: OpaquePointer

  public init() throws {
    var context: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_context_create(&context, &error)

    guard status == SWIFTZFS_OK, let context else {
      throw Self.makeError(
        operation: "swiftzfs_context_create",
        error: error
      )
    }

    if let error {
      swiftzfs_error_destroy(error)
    }
    self.handle = context
  }

  deinit {
    swiftzfs_context_destroy(handle)
  }

  public func pools() throws -> [ZPool] {
    var list: OpaquePointer?
    var error: OpaquePointer?
    let status = swiftzfs_context_pools(handle, &list, &error)

    guard status == SWIFTZFS_OK, let list else {
      throw Self.makeError(operation: "pools", error: error)
    }
    if let error {
      swiftzfs_error_destroy(error)
    }
    defer {
      swiftzfs_pool_list_destroy(list)
    }

    let count = swiftzfs_pool_list_count(list)
    var result: [ZPool] = []
    result.reserveCapacity(count)

    for index in 0..<count {
      guard let pool = swiftzfs_pool_list_take_at(list, index) else {
        continue
      }
      defer {
        swiftzfs_pool_destroy(pool)
      }

      var name: UnsafeMutablePointer<CChar>?
      var poolError: OpaquePointer?
      let poolStatus = swiftzfs_pool_copy_name(pool, &name, &poolError)
      guard poolStatus == SWIFTZFS_OK, let name else {
        throw Self.makeError(operation: "pool.name", error: poolError)
      }
      if let poolError {
        swiftzfs_error_destroy(poolError)
      }
      defer {
        swiftzfs_string_free(name)
      }

      result.append(ZPool(name: String(cString: name)))
    }

    return result
  }

  private static func makeError(
    operation: String,
    error: OpaquePointer?
  ) -> ZFSError {
    guard let error else {
      return ZFSError(
        operation: operation,
        code: SWIFTZFS_ERROR_UNKNOWN,
        systemError: 0,
        message: "unknown SwiftZFS error"
      )
    }

    defer {
      swiftzfs_error_destroy(error)
    }

    let message: String
    if let cString = swiftzfs_error_message(error) {
      message = String(cString: cString)
    } else {
      message = "unknown SwiftZFS error"
    }

    return ZFSError(
      operation: operation,
      code: swiftzfs_error_code(error),
      systemError: swiftzfs_error_system_error(error),
      message: message
    )
  }
}
