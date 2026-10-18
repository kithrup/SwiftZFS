import CZFS
import Foundation

/// Options controlling the format of a snapshot send stream.
/// Equality and hashing include all option values.
public struct SendOptions: Hashable {
  /// Permit embedded data records.
  public var embeddedData: Bool

  /// Permit blocks larger than 128 KiB.
  public var largeBlocks: Bool

  /// Preserve compressed blocks in the stream.
  public var compressed: Bool

  /// Produce a raw encrypted send stream.
  public var raw: Bool

  /// Create send options. Defaults produce a conservative stream.
  ///
  /// - Parameters:
  ///   - embeddedData: Permit embedded data records.
  ///   - largeBlocks: Permit blocks larger than 128 KiB.
  ///   - compressed: Preserve compressed blocks.
  ///   - raw: Produce a raw encrypted stream.
  public init(
    embeddedData: Bool = false,
    largeBlocks: Bool = false,
    compressed: Bool = false,
    raw: Bool = false
  ) {
    self.embeddedData = embeddedData
    self.largeBlocks = largeBlocks
    self.compressed = compressed
    self.raw = raw
  }

  var cValue: swiftzfs_send_options_t {
    swiftzfs_send_options_t(
      embedded_data: embeddedData,
      large_blocks: largeBlocks,
      compressed: compressed,
      raw: raw
    )
  }
}

/// An explicit dataset selection used to encode replication metadata.
///
/// The snapshot array contains only targets to send, ordered by creation
/// transaction. The optional base is catalogued automatically. This value retains
/// its dataset and snapshots, and performs no enumeration or OpenZFS I/O.
public struct ReplicationDataset {
  /// Filesystem or volume included in the package.
  public let dataset: Dataset

  /// Existing incremental base, or `nil` for a new dataset.
  /// For a clone without a local base, pass its origin snapshot here.
  public let from: Snapshot?

  /// Selected target snapshots in creation order, excluding the base.
  public let snapshots: [Snapshot]

  /// Snapshots already on the receiver, included in metadata without sending data.
  /// Use this to retain older history in the catalogue during incremental replication.
  public let retainedSnapshots: [Snapshot]

  /// Describe one dataset without starting a send.
  /// - Parameters:
  ///   - dataset: Filesystem or volume to include.
  ///   - from: Existing base or clone origin, or `nil` for a full send.
  ///   - snapshots: Nonempty array of selected targets, oldest first.
  ///   - retainedSnapshots: Additional snapshots to catalogue without sending.
  /// Selection validity is checked when generating the replication header.
  public init(
    dataset: Dataset, from: Snapshot? = nil, snapshots: [Snapshot],
    retainedSnapshots: [Snapshot] = []
  ) {
    self.dataset = dataset
    self.from = from
    self.snapshots = snapshots
    self.retainedSnapshots = retainedSnapshots
  }
}

/// A single-consumer sequence of chunks from a ZFS snapshot send.
///
/// The stream reads up to 128 KiB at a time. Reading can block while OpenZFS
/// produces data. Releasing an unfinished stream drains the producer and may
/// block until the send completes.
public final class SendStream: AsyncSequence {
  /// A chunk of send stream data.
  public typealias Element = Data

  /// Pool containing the snapshot being sent, retained for the stream lifetime.
  public let pool: Pool

  /// Incremental base snapshot, or `nil` for a full send.
  /// Retained even after the stream finishes.
  public let from: Snapshot?

  /// Target snapshot being sent, retained even after the stream finishes.
  public let snapshot: Snapshot

  /// Encode the opening records of a compound package ending at a snapshot.
  ///
  /// Write this once, followed by each selected snapshot send stream, and then
  /// ``compoundFooter()``. Each incremental send must use the preceding included
  /// snapshot as its base. The client chooses and orders all streams.
  ///
  /// The header has no replication metadata, matching `zfs send -I` without
  /// `-R` or `-p`. It does not preserve dataset properties or clone relationships.
  /// No dataset enumeration or stream production is performed.
  /// - Parameter snapshot: Final target snapshot used to name the package.
  /// - Returns: A compound BEGIN and its checksummed header END as binary data.
  /// - Throws: `ZFSError` if the header cannot be encoded.
  public static func compoundHeader(for snapshot: Snapshot) throws -> Data {
    var bytes: UnsafeMutablePointer<UInt8>?
    var size = 0
    var error: OpaquePointer?
    let status = snapshot.name.withCString {
      swiftzfs_send_compound_header($0, &bytes, &size, &error)
    }
    defer { swiftzfs_bytes_free(bytes) }
    try checkSwiftZFSStatus(status, operation: "send.compoundHeader", error: error)
    guard let bytes else {
      throw invariantError("send.compoundHeader", "C shim returned no header bytes")
    }
    return Data(bytes: bytes, count: size)
  }

  /// Encode a compound header with caller-selected replication metadata.
  ///
  /// Captures local/received dataset and snapshot properties, GUIDs, parent
  /// relationships, clone origins, and encryption roots. Only the selected
  /// targets, their bases, and explicit retained snapshots appear in the catalogue. No hierarchy or snapshot
  /// enumeration is performed. All datasets must belong to the package root or
  /// its descendants, with every intervening ancestor included. All catalogued
  /// snapshots must be no newer than the final root snapshot.
  ///
  /// Write this header, send exactly the selected targets from their specified
  /// bases, and finish with ``compoundFooter()``. Emit parents and required origin
  /// snapshots before dependents. Encrypted datasets require raw streams. Use the
  /// same send options for the header and every substream.
  ///
  /// The catalogue is authoritative for `zfs receive -F`: omitted datasets and
  /// snapshots may be destroyed at the destination. Snapshot holds are omitted.
  /// - Parameters:
  ///   - snapshot: Final root snapshot, which must end the root dataset selection.
  ///   - datasets: Included datasets, each with ordered targets and an optional base.
  ///   - options: Features used by the selected send streams.
  /// - Returns: Compound BEGIN, XDR metadata, and checksummed header END.
  /// - Throws: `ZFSError` for an invalid selection or a property/encoding failure.
  public static func replicationHeader(
    for snapshot: Snapshot, datasets: [ReplicationDataset], options: SendOptions = SendOptions()
  ) throws -> Data {
    // Flatten handles into one stable allocation while the C descriptors borrow
    // slices of it. All Swift objects remain strongly held by datasets during I/O.
    let handles: [OpaquePointer?] = datasets.flatMap {
      ($0.snapshots + $0.retainedSnapshots).map { $0.handle }
    }
    var bytes: UnsafeMutablePointer<UInt8>?
    var size = 0
    var error: OpaquePointer?
    var cOptions = options.cValue
    let status = withExtendedLifetime((snapshot, datasets)) {
      handles.withUnsafeBufferPointer { buffer in
        var offset = 0
        let selections = datasets.map { item in
          let first = buffer.baseAddress.map { $0.advanced(by: offset) }
          offset += item.snapshots.count
          let retained = buffer.baseAddress.map { $0.advanced(by: offset) }
          offset += item.retainedSnapshots.count
          return swiftzfs_replication_dataset_t(
            dataset: item.dataset.handle, from_snapshot: item.from?.handle,
            snapshots: first, snapshot_count: item.snapshots.count,
            retained_snapshots: retained, retained_snapshot_count: item.retainedSnapshots.count)
        }
        return selections.withUnsafeBufferPointer { selection in
          swiftzfs_send_replication_header(
            snapshot.handle, selection.baseAddress, selection.count, &cOptions, &bytes, &size,
            &error)
        }
      }
    }
    defer { swiftzfs_bytes_free(bytes) }
    try checkSwiftZFSStatus(status, operation: "send.replicationHeader", error: error)
    guard let bytes else {
      throw invariantError("send.replicationHeader", "C shim returned no header bytes")
    }
    return Data(bytes: bytes, count: size)
  }

  /// Encode the final record of a compound package.
  ///
  /// Write this once after all selected snapshot send streams finish successfully.
  /// Each snapshot stream already contains its own END record; this additional
  /// END terminates the package. It has a zero checksum and needs no running state.
  /// - Returns: The final compound END as binary data.
  /// - Throws: `ZFSError` if the footer cannot be encoded.
  public static func compoundFooter() throws -> Data {
    var bytes: UnsafeMutablePointer<UInt8>?
    var size = 0
    var error: OpaquePointer?
    let status = swiftzfs_send_compound_footer(&bytes, &size, &error)
    defer { swiftzfs_bytes_free(bytes) }
    try checkSwiftZFSStatus(status, operation: "send.compoundFooter", error: error)
    guard let bytes else {
      throw invariantError("send.compoundFooter", "C shim returned no footer bytes")
    }
    return Data(bytes: bytes, count: size)
  }

  private var handle: OpaquePointer?

  init(handle: OpaquePointer, snapshot: Snapshot, from: Snapshot?) {
    self.handle = handle
    self.pool = snapshot.pool
    self.from = from
    self.snapshot = snapshot
  }

  deinit {
    close()
  }

  private func close() {
    if let handle {
      self.handle = nil
      swiftzfs_send_stream_destroy(handle)
    }
  }

  /// Create an iterator over the stream's next chunks.
  ///
  /// The stream supports one consumer; do not make multiple iterators.
  /// - Returns: An iterator reading the remaining stream data.
  public func makeAsyncIterator() -> Iterator {
    Iterator(stream: self)
  }

  /// An iterator over send stream chunks.
  public struct Iterator: AsyncIteratorProtocol {
    private let stream: SendStream

    fileprivate init(stream: SendStream) {
      self.stream = stream
    }

    /// Read the next chunk.
    ///
    /// - Returns: Up to 128 KiB of data, or `nil` at end of stream.
    /// - Throws: `ZFSError` if OpenZFS or the stream read fails.
    public mutating func next() async throws -> Data? {
      guard let handle = stream.handle else { return nil }
      var data = Data(count: 128 * 1024)
      var bytesRead = 0
      var error: OpaquePointer?
      let status = data.withUnsafeMutableBytes { buffer in
        swiftzfs_send_stream_read(handle, buffer.baseAddress, buffer.count, &bytesRead, &error)
      }
      do {
        try checkSwiftZFSStatus(status, operation: "snapshot.send.read", error: error)
      } catch {
        stream.close()
        throw error
      }
      if bytesRead == 0 {
        stream.close()
        return nil
      }
      data.removeSubrange(bytesRead..<data.count)
      return data
    }
  }
}
