/// An error reported by SwiftZFS or OpenZFS.
public struct ZFSError: Error, CustomStringConvertible, Sendable {
  /// High-level error classifications that are stable across the C ABI.
  public enum Code: Sendable {
    case unknown
    case initializationFailed
    case invalidArgument
    case permissionDenied
    case notFound
    case alreadyExists
    case ioError
    case noMemory
    case noSuchPool
    case poolUnavailable
    case poolBusy
    case poolFaulted
    case invalidPool
    case unsupported
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

  public init(operation: String, code: Code, systemError: Int32, message: String) {
    self.operation = operation
    self.code = code
    self.systemError = systemError
    self.message = message
  }

  public var description: String {
    if systemError != 0 {
      return "\(operation): \(message) (system error \(systemError))"
    }
    return "\(operation): \(message)"
  }
}
