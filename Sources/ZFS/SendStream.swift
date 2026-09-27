import CZFS
import Foundation

/// Options controlling the format of a snapshot send stream.
public struct SendOptions {
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

/// A single-consumer sequence of chunks from a ZFS snapshot send.
///
/// The stream reads up to 128 KiB at a time. Reading can block while OpenZFS
/// produces data. Releasing an unfinished stream drains the producer and may
/// block until the send completes.
public final class SendStream: AsyncSequence {
  /// A chunk of send stream data.
  public typealias Element = Data

  private var handle: OpaquePointer?

  init(handle: OpaquePointer) {
    self.handle = handle
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
