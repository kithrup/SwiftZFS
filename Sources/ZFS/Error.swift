/// An error reported by SwiftZFS or OpenZFS.
public struct ZFSError: Error, CustomStringConvertible, Sendable {
  /// High-level error classifications that are stable across the C ABI.
  public enum Code: Sendable {
    /// The failure could not be classified more specifically.
    case unknown
    /// OpenZFS initialization failed.
    case initializationFailed
    /// A supplied argument is invalid.
    case invalidArgument
    /// The operation was not permitted.
    case permissionDenied
    /// The requested dataset, snapshot, or property was not found.
    case notFound
    /// An object with the requested name already exists.
    case alreadyExists
    /// An underlying input or output operation failed.
    case ioError
    /// Memory allocation failed.
    case noMemory
    /// The named pool does not exist.
    case noSuchPool
    /// The pool exists but cannot be used for this operation.
    case poolUnavailable
    /// The pool is busy.
    case poolBusy
    /// The pool is faulted.
    case poolFaulted
    /// Pool configuration or metadata is invalid.
    case invalidPool
    /// The requested feature is unsupported.
    case unsupported
    /// The requested operation has not been implemented.
    case notImplemented
  }

  /// Operation being performed when the error occurred.
  public let operation: String

  /// High-level SwiftZFS error classification.
  public let code: Code

  /// Native error number, or zero when no native error applies.
  public let systemError: Int32

  /// Human-readable error description.
  public let message: String

  /// Create an error with operation context and a stable classification.
  ///
  /// - Parameters:
  ///   - operation: Operation being performed.
  ///   - code: High-level error classification.
  ///   - systemError: Native error number, or zero if none applies.
  ///   - message: Human-readable explanation.
  public init(operation: String, code: Code, systemError: Int32, message: String) {
    self.operation = operation
    self.code = code
    self.systemError = systemError
    self.message = message
  }

  /// A readable description containing the operation, message, and native
  /// error number when one is available.
  public var description: String {
    if systemError != 0 {
      return "\(operation): \(message) (system error \(systemError))"
    }
    return "\(operation): \(message)"
  }
}
