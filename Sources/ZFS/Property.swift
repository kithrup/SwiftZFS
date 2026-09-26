/// A ZFS property value together with its source and metadata.
public struct Property: Hashable, Sendable {
  /// Source from which the effective property value was obtained.
  public enum Source: Hashable, Sendable {
    case none
    case defaultValue
    case temporary
    case local
    case inherited
    case received
    case unknown
  }

  /// Native representation used by the ZFS property.
  public enum ValueType: Hashable, Sendable {
    case number
    case string
    case index
    case unknown
  }

  public let name: String
  public let value: String
  public let source: Source
  public let valueType: ValueType
  public let isReadOnly: Bool

  public init(
    name: String,
    value: String,
    source: Source,
    valueType: ValueType,
    isReadOnly: Bool
  ) {
    self.name = name
    self.value = value
    self.source = source
    self.valueType = valueType
    self.isReadOnly = isReadOnly
  }
}
