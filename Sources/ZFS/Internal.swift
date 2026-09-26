import CZFS

@inline(__always)
func swiftZFSErrorCode(_ code: swiftzfs_error_code_t) -> ZFSError.Code {
  if code == SWIFTZFS_ERROR_INITIALIZATION_FAILED { return .initializationFailed }
  if code == SWIFTZFS_ERROR_INVALID_ARGUMENT { return .invalidArgument }
  if code == SWIFTZFS_ERROR_PERMISSION_DENIED { return .permissionDenied }
  if code == SWIFTZFS_ERROR_NOT_FOUND { return .notFound }
  if code == SWIFTZFS_ERROR_ALREADY_EXISTS { return .alreadyExists }
  if code == SWIFTZFS_ERROR_IO { return .ioError }
  if code == SWIFTZFS_ERROR_NO_MEMORY { return .noMemory }
  if code == SWIFTZFS_ERROR_NO_SUCH_POOL { return .noSuchPool }
  if code == SWIFTZFS_ERROR_POOL_UNAVAILABLE { return .poolUnavailable }
  if code == SWIFTZFS_ERROR_POOL_BUSY { return .poolBusy }
  if code == SWIFTZFS_ERROR_POOL_FAULTED { return .poolFaulted }
  if code == SWIFTZFS_ERROR_INVALID_POOL { return .invalidPool }
  if code == SWIFTZFS_ERROR_UNSUPPORTED { return .unsupported }
  if code == SWIFTZFS_ERROR_NOT_IMPLEMENTED { return .notImplemented }
  return .unknown
}

func makeSwiftZFSError(operation: String, error: OpaquePointer?) -> ZFSError {
  guard let error else {
    return ZFSError(
      operation: operation,
      code: .unknown,
      systemError: 0,
      message: "unknown SwiftZFS error"
    )
  }

  defer { swiftzfs_error_destroy(error) }

  let message =
    swiftzfs_error_message(error).map(String.init(cString:))
    ?? "unknown SwiftZFS error"

  return ZFSError(
    operation: operation,
    code: swiftZFSErrorCode(swiftzfs_error_code(error)),
    systemError: swiftzfs_error_system_error(error),
    message: message
  )
}

func checkSwiftZFSStatus(
  _ status: swiftzfs_status_t,
  operation: String,
  error: OpaquePointer?
) throws {
  guard status == SWIFTZFS_OK else {
    throw makeSwiftZFSError(operation: operation, error: error)
  }
  if let error {
    swiftzfs_error_destroy(error)
  }
}

func invariantError(_ operation: String, _ message: String) -> ZFSError {
  ZFSError(operation: operation, code: .unknown, systemError: 0, message: message)
}

func takeCString(_ pointer: UnsafeMutablePointer<CChar>?) -> String? {
  guard let pointer else { return nil }
  defer { swiftzfs_string_free(pointer) }
  return String(cString: pointer)
}

func propertySource(_ source: swiftzfs_property_source_t) -> Property.Source {
  if source == SWIFTZFS_PROPERTY_SOURCE_NONE { return .none }
  if source == SWIFTZFS_PROPERTY_SOURCE_DEFAULT { return .defaultValue }
  if source == SWIFTZFS_PROPERTY_SOURCE_TEMPORARY { return .temporary }
  if source == SWIFTZFS_PROPERTY_SOURCE_LOCAL { return .local }
  if source == SWIFTZFS_PROPERTY_SOURCE_INHERITED { return .inherited }
  if source == SWIFTZFS_PROPERTY_SOURCE_RECEIVED { return .received }
  return .unknown
}

func propertyType(_ type: swiftzfs_property_type_t) -> Property.ValueType {
  if type == SWIFTZFS_PROPERTY_NUMBER { return .number }
  if type == SWIFTZFS_PROPERTY_STRING { return .string }
  if type == SWIFTZFS_PROPERTY_INDEX { return .index }
  return .unknown
}

func decodeProperties(from list: OpaquePointer) -> [Property] {
  let count = swiftzfs_property_list_count(list)
  var result: [Property] = []
  result.reserveCapacity(count)

  for index in 0..<count {
    guard
      let name = swiftzfs_property_list_name_at(list, index),
      let value = swiftzfs_property_list_value_at(list, index)
    else {
      continue
    }

    result.append(
      Property(
        name: String(cString: name),
        value: String(cString: value),
        source: propertySource(swiftzfs_property_list_source_at(list, index)),
        valueType: propertyType(swiftzfs_property_list_type_at(list, index)),
        isReadOnly: swiftzfs_property_list_readonly_at(list, index)
      )
    )
  }

  return result
}
