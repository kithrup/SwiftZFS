import CZFS

public struct ZFSError: Error, CustomStringConvertible, Sendable {
    public let operation: String
    public let zfsCode: Int32
    public let message: String

    public var description: String {
        if zfsCode != 0 {
            return "\(operation): \(message) (libzfs error \(zfsCode))"
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
        let status = swiftzfs_context_create(&context)

        guard status == 0, let context else {
            throw ZFSError(
                operation: "libzfs_init",
                zfsCode: 0,
                message: "unable to initialize libzfs"
            )
        }

        self.handle = context
    }

    deinit {
        swiftzfs_context_destroy(handle)
    }

    public func pools() throws -> [ZPool] {
        var list: OpaquePointer?
        let status = swiftzfs_pool_list_create(handle, &list)

        guard status == 0, let list else {
            throw currentError(operation: "zpool_iter")
        }

        defer {
            swiftzfs_pool_list_destroy(list)
        }

        let count = swiftzfs_pool_list_count(list)
        var result: [ZPool] = []
        result.reserveCapacity(count)

        for index in 0..<count {
            guard let name = swiftzfs_pool_list_name_at(list, index) else {
                continue
            }
            result.append(ZPool(name: String(cString: name)))
        }

        return result
    }

    private func currentError(operation: String) -> ZFSError {
        let code = swiftzfs_context_last_zfs_error(handle)
        let message: String

        if let cString = swiftzfs_context_last_error_description(handle) {
            message = String(cString: cString)
        } else {
            message = "unknown libzfs error"
        }

        return ZFSError(operation: operation, zfsCode: code, message: message)
    }
}
