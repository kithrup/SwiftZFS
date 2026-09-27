/// A ZFS property value together with its source and metadata.
public struct Property: Hashable, Sendable {
  /// Source from which the effective property value was obtained.
  public enum Source: Hashable, Sendable {
    /// No source was reported.
    case none
    /// The value comes from the OpenZFS default.
    case defaultValue
    /// The value is temporarily overridden.
    case temporary
    /// The value is set on this object.
    case local
    /// The value is inherited from a parent dataset.
    case inherited
    /// The value was received from another system.
    case received
    /// OpenZFS reported a source this API does not recognize.
    case unknown
  }

  /// Native representation used by the ZFS property.
  public enum ValueType: Hashable, Sendable {
    /// An integer value.
    case number
    /// A text value.
    case string
    /// A named value backed by an integer index.
    case index
    /// OpenZFS reported a value type this API does not recognize.
    case unknown
  }

  /// ZFS property name.
  public let name: String
  /// Formatted effective value.
  public let value: String
  /// Where the effective value came from.
  public let source: Source
  /// Native representation of the property value.
  public let valueType: ValueType
  /// Whether the property cannot be changed.
  public let isReadOnly: Bool

  /// Create a property value with its source and metadata.
  ///
  /// - Parameters:
  ///   - name: ZFS property name.
  ///   - value: Formatted effective value.
  ///   - source: Where the effective value came from.
  ///   - valueType: Native representation of the value.
  ///   - isReadOnly: Whether the property cannot be changed.
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
