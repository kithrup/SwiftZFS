import Foundation
import XCTest

@testable import ZFS

final class ZFSTests: XCTestCase {
  func testPoolAndDatasetIdentity() throws {
    let pools = try ZFS().pools()
    guard let pool = pools.first else { throw XCTSkip("No imported pool") }
    let reopenedPool = try ZFS().pool(named: pool.name)
    XCTAssertFalse(pool === reopenedPool)
    XCTAssertEqual(pool, reopenedPool)
    XCTAssertEqual(pool.hashValue, reopenedPool.hashValue)
    XCTAssertEqual(Set([pool, reopenedPool]).count, 1)
    XCTAssertEqual([pool: "found"][reopenedPool], "found")
    for other in pools.dropFirst() {
      XCTAssertNotEqual(pool, other)
    }

    let datasets = try pool.datasets()
    let reopenedDatasets = try reopenedPool.datasets()
    XCTAssertEqual(datasets, reopenedDatasets)
    XCTAssertEqual(Set([datasets, reopenedDatasets]).count, 1)
    XCTAssertEqual(Set(datasets.filesystems), Set(reopenedDatasets.filesystems))
    XCTAssertEqual(Set(datasets.volumes), Set(reopenedDatasets.volumes))
    let all: [Dataset] = datasets.filesystems + datasets.volumes
    let reopened: [Dataset] = reopenedDatasets.filesystems + reopenedDatasets.volumes
    let byName = Dictionary(uniqueKeysWithValues: reopened.map { ($0.name, $0) })
    XCTAssertEqual(Set(all).count, all.count)
    for dataset in all {
      let other = try XCTUnwrap(byName[dataset.name])
      XCTAssertFalse(dataset === other)
      XCTAssertEqual(dataset, other)
      XCTAssertEqual(dataset.hashValue, other.hashValue)
      XCTAssertEqual([dataset: "found"][other], "found")
    }
    let root = try XCTUnwrap(all.first { $0.name == pool.name })
    for child in all where child.name != pool.name {
      XCTAssertNotEqual(root, child)
    }
    if datasets.filesystems.count > 1 {
      XCTAssertNotEqual(
        datasets,
        DatasetCollection(
          filesystems: Array(datasets.filesystems.reversed()), volumes: datasets.volumes)
      )
    }
  }

  func testHashableValues() {
    XCTAssertEqual(Set([SendOptions(), SendOptions()]).count, 1)
    for options in [
      SendOptions(embeddedData: true), SendOptions(largeBlocks: true),
      SendOptions(compressed: true), SendOptions(raw: true),
    ] {
      XCTAssertNotEqual(SendOptions(), options)
    }
    let error = ZFSError(operation: "test", code: .ioError, systemError: 5, message: "failed")
    let same = ZFSError(operation: "test", code: .ioError, systemError: 5, message: "failed")
    XCTAssertEqual(error, same)
    XCTAssertEqual(Set([error, same]).count, 1)
    for different in [
      ZFSError(operation: "other", code: .ioError, systemError: 5, message: "failed"),
      ZFSError(operation: "test", code: .unknown, systemError: 5, message: "failed"),
      ZFSError(operation: "test", code: .ioError, systemError: 0, message: "failed"),
      ZFSError(operation: "test", code: .ioError, systemError: 5, message: "other"),
    ] {
      XCTAssertNotEqual(error, different)
    }
  }

  func testDatasetParentsAndPaths() throws {
    guard let pool = try ZFS().pools().first else { throw XCTSkip("No imported pool") }
    let datasets = try pool.datasets()
    let root = try XCTUnwrap(datasets.filesystems.first { $0.name == pool.name })
    XCTAssertNil(root.parent)
    var seen = Set<String>()
    func check(_ dataset: Dataset) throws {
      XCTAssertTrue(seen.insert(dataset.path).inserted)
      XCTAssertEqual(dataset.path, dataset.name)
      XCTAssertTrue(dataset.pool === pool)
      if dataset.name != pool.name {
        let expected = String(dataset.name.prefix(upTo: dataset.name.lastIndex(of: "/")!))
        XCTAssertEqual(dataset.parent?.path, expected)
      }
      for child in try dataset.children() {
        XCTAssertTrue(child.parent === dataset)
        XCTAssertTrue(child.pool === pool)
        XCTAssertNotEqual(child.path, dataset.path)
      }
    }
    for dataset in datasets.filesystems { try check(dataset) }
    for dataset in datasets.volumes { try check(dataset) }
    // A parent chain remains complete when the pool and collection go away.
    let retained = try { () -> Dataset in
      let pool = try ZFS().pool(named: pool.name)
      return try pool.descendants().last ?? pool.dataset(named: pool.name)
    }()
    var ancestor: Dataset? = retained
    var ancestors = Set<String>()
    while let current = ancestor {
      XCTAssertTrue(ancestors.insert(current.path).inserted)
      ancestor = current.parent
    }
    XCTAssertTrue(ancestors.contains(pool.name))
    XCTAssertEqual(retained.pool.guid, pool.guid)
    _ = try retained.pool.properties()
  }

  func testDatasetRetainsPoolWithoutCycle() throws {
    guard let poolName = try ZFS().pools().first?.name else {
      throw XCTSkip("No imported pool")
    }
    weak var retainedPool: Pool?
    var dataset: Dataset? = try { () -> Dataset in
      let pool = try ZFS().pool(named: poolName)
      retainedPool = pool
      return try pool.dataset(named: poolName)
    }()
    XCTAssertNotNil(retainedPool)
    XCTAssertTrue(dataset?.pool === retainedPool)
    XCTAssertEqual(dataset?.pool.name, poolName)
    _ = try dataset?.pool.properties()
    dataset = nil
    XCTAssertNil(retainedPool)
  }

  func testTypedProperties() throws {
    let pools = try ZFS().pools()
    guard let pool = pools.first else { throw XCTSkip("No imported pool") }
    let dataset = try pool.dataset(named: pool.name)
    XCTAssertEqual(dataset.type, .filesystem)
    XCTAssertGreaterThan(dataset.creationTime.timeIntervalSince1970, 0)
    XCTAssertLessThanOrEqual(dataset.creationTime, Date())
    XCTAssertEqual(try dataset.compression, try dataset.property(named: "compression").value)
    XCTAssertEqual(try dataset.isReadOnly, try dataset.property(named: "readonly").value == "on")
    XCTAssertGreaterThan(try pool.allocatedSpace + pool.freeSpace, 0)
    let readonly = try XCTUnwrap(pool.properties().first { $0.name == "readonly" })
    XCTAssertEqual(try pool.isReadOnly, readonly.value == "on")
    // Exercise byte-valued getters even on full pools and empty datasets.
    _ = try dataset.allocatedSpace
    _ = try dataset.freeSpace
    let filesystem = try XCTUnwrap(dataset as? Filesystem)
    XCTAssertFalse(try filesystem.mountpoint.isEmpty)
    _ = try filesystem.isMounted

    if let snapshot = try dataset.snapshots().first {
      XCTAssertTrue(snapshot.parent === dataset)
      XCTAssertTrue(snapshot.pool === pool)
      XCTAssertEqual(snapshot.path, snapshot.name)
      XCTAssertEqual(snapshot.type, .snapshot)
      XCTAssertTrue(try snapshot.isReadOnly)
      XCTAssertGreaterThan(snapshot.creationTime.timeIntervalSince1970, 0)
      _ = try snapshot.allocatedSpace
      XCTAssertThrowsError(try snapshot.freeSpace)
      XCTAssertThrowsError(try snapshot.compression)
    }
    // Identity stays available after the discovering context has been destroyed.
    XCTAssertEqual(dataset.type, .filesystem)
    XCTAssertGreaterThan(dataset.guid, 0)
  }

  func testPropertyValueType() {
    let property = Property(
      name: "compression",
      value: "lz4",
      source: .local,
      valueType: .index,
      isReadOnly: false
    )

    XCTAssertEqual(property.name, "compression")
    XCTAssertEqual(property.value, "lz4")
    XCTAssertEqual(property.source, .local)
    XCTAssertEqual(property.valueType, .index)
    XCTAssertFalse(property.isReadOnly)
  }

  func testErrorDescription() {
    let error = ZFSError(
      operation: "test",
      code: .ioError,
      systemError: 5,
      message: "input/output error"
    )
    XCTAssertEqual(error.description, "test: input/output error (system error 5)")
  }

  func testSnapshotLookup() throws {
    guard
      let datasetName = ProcessInfo.processInfo.environment["SWIFTZFS_TEST_DATASET"],
      let snapshotName = ProcessInfo.processInfo.environment["SWIFTZFS_TEST_SNAPSHOT"]
    else {
      throw XCTSkip(
        "Set SWIFTZFS_TEST_DATASET and SWIFTZFS_TEST_SNAPSHOT to run against a local snapshot")
    }

    let poolName = String(datasetName.split(separator: "/", maxSplits: 1)[0])
    let pool = try XCTUnwrap(ZFS().pools().first { $0.name == poolName })
    let dataset = try pool.dataset(named: datasetName)

    let snapshot = try dataset.snapshot(named: snapshotName)
    XCTAssertEqual(snapshot.name, "\(datasetName)@\(snapshotName)")
    XCTAssertTrue(snapshot.isSnapshot)
    XCTAssertTrue(snapshot.parent === dataset)
    XCTAssertTrue(snapshot.pool === pool)

    let openedSnapshot = try pool.dataset(named: snapshot.name)
    XCTAssertEqual(openedSnapshot.name, snapshot.name)
    XCTAssertTrue(openedSnapshot.isSnapshot)
    XCTAssertFalse(snapshot === openedSnapshot)
    XCTAssertEqual(snapshot, openedSnapshot)
    XCTAssertEqual(snapshot.hashValue, openedSnapshot.hashValue)
    let typedSnapshot = try XCTUnwrap(openedSnapshot as? Snapshot)
    XCTAssertEqual(Set([snapshot, typedSnapshot]).count, 1)
    XCTAssertNotEqual(snapshot, dataset)
    XCTAssertEqual(openedSnapshot.parent?.path, dataset.path)
    XCTAssertTrue(openedSnapshot.pool === pool)
    XCTAssertTrue(openedSnapshot.parent?.pool === pool)

    XCTAssertThrowsError(try dataset.snapshot(named: "swiftzfs-missing-\(UUID())")) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .notFound = error.code else {
        XCTFail("Expected .notFound, got \(error.code)")
        return
      }
    }

    XCTAssertThrowsError(try snapshot.snapshot(named: snapshotName)) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .invalidArgument = error.code else {
        XCTFail("Expected .invalidArgument, got \(error.code)")
        return
      }
    }
  }

  func testSnapshotCreationRejectsInvalidInput() throws {
    guard let pool = try ZFS().pools().first else { throw XCTSkip("No imported pool") }
    let dataset = try pool.dataset(named: pool.name)
    for name in ["", "invalid\0name", "invalid/name", "invalid@name"] {
      XCTAssertThrowsError(try dataset.createSnapshot(named: name)) { error in
        XCTAssertEqual((error as? ZFSError)?.code, .invalidArgument)
      }
    }
    for properties in [
      ["org.swiftzfs:invalid\0key": "value"],
      ["org.swiftzfs:test": "invalid\0value"],
      ["compression": "lz4"],
    ] {
      XCTAssertThrowsError(
        try dataset.createSnapshot(named: "swiftzfs-invalid-\(UUID())", properties: properties)
      ) { error in
        XCTAssertEqual((error as? ZFSError)?.code, .invalidArgument)
      }
    }
  }

  /// Opt-in integration test: creates and removes only uniquely named test snapshots.
  func testSnapshotCreation() throws {
    try checkSnapshotCreation(environmentKey: "SWIFTZFS_TEST_CREATE_DATASET", properties: [:])
  }

  func testSnapshotCreationWithProperties() throws {
    try checkSnapshotCreation(
      environmentKey: "SWIFTZFS_TEST_CREATE_PROPERTIES_DATASET",
      properties: ["org.swiftzfs:test": "snapshot café", "org.swiftzfs:empty": ""])
  }

  private func checkSnapshotCreation(environmentKey: String, properties: [String: String]) throws {
    guard let datasetName = ProcessInfo.processInfo.environment[environmentKey]
    else {
      throw XCTSkip(
        "Set \(environmentKey) to a test filesystem or volume with snapshot and, if needed, userprop permissions"
      )
    }
    let poolName = String(datasetName.split(separator: "/", maxSplits: 1)[0])
    let dataset = try ZFS().pool(named: poolName).dataset(named: datasetName)
    let name = "swiftzfs-test-\(UUID())"
    let snapshot = try dataset.createSnapshot(named: name, properties: properties)
    defer {
      // There is no Swift destruction API yet; remove exactly the snapshot we created.
      let cleanup = Process()
      cleanup.executableURL = URL(fileURLWithPath: "/usr/bin/env")
      cleanup.arguments = ["zfs", "destroy", snapshot.name]
      do {
        try cleanup.run()
        cleanup.waitUntilExit()
        XCTAssertEqual(cleanup.terminationStatus, 0, "Failed to remove \(snapshot.name)")
      } catch {
        XCTFail("Failed to remove \(snapshot.name): \(error)")
      }
    }
    XCTAssertEqual(snapshot.name, "\(datasetName)@\(name)")
    XCTAssertTrue(snapshot.isSnapshot)
    XCTAssertEqual(snapshot.type, .snapshot)
    XCTAssertTrue(snapshot.parent === dataset)
    XCTAssertTrue(snapshot.pool === dataset.pool)
    XCTAssertGreaterThan(snapshot.guid, 0)
    XCTAssertTrue(try snapshot.isReadOnly)
    XCTAssertEqual(try dataset.snapshot(named: name).guid, snapshot.guid)
    XCTAssertTrue(try dataset.snapshots().contains { $0.guid == snapshot.guid })
    for (key, value) in properties {
      XCTAssertEqual(try snapshot.property(named: key).value, value)
    }
    XCTAssertThrowsError(try dataset.createSnapshot(named: name)) { error in
      XCTAssertTrue(error is ZFSError)
    }
    XCTAssertThrowsError(try snapshot.createSnapshot(named: "invalid")) { error in
      XCTAssertEqual((error as? ZFSError)?.code, .invalidArgument)
    }
  }

  func testSnapshotCreationOrder() throws {
    guard let datasetName = ProcessInfo.processInfo.environment["SWIFTZFS_TEST_DATASET"]
    else {
      throw XCTSkip("Set SWIFTZFS_TEST_DATASET to check snapshot creation order")
    }
    let poolName = String(datasetName.split(separator: "/", maxSplits: 1)[0])
    let dataset = try ZFS().pool(named: poolName).dataset(named: datasetName)
    let snapshots = try dataset.snapshots()
    guard snapshots.count > 1 else {
      throw XCTSkip("At least two existing snapshots are needed to check ordering")
    }
    let transactions = try snapshots.map {
      try XCTUnwrap(UInt64($0.property(named: "createtxg").value))
    }
    XCTAssertEqual(transactions, transactions.sorted())
    XCTAssertEqual(Set(snapshots).count, snapshots.count)
  }

  func testReplicationHeaderSelection() throws {
    guard let datasetName = ProcessInfo.processInfo.environment["SWIFTZFS_TEST_DATASET"] else {
      throw XCTSkip(
        "Set SWIFTZFS_TEST_DATASET to test replication metadata against existing snapshots")
    }
    let poolName = String(datasetName.split(separator: "/", maxSplits: 1)[0])
    let dataset = try ZFS().pool(named: poolName).dataset(named: datasetName)
    let snapshots = try dataset.snapshots()
    guard snapshots.count >= 3 else { throw XCTSkip("At least three snapshots are needed") }
    let base = snapshots[0]
    let omitted = snapshots[1]
    let target = snapshots[2]
    let options = SendOptions(raw: true)
    let selection = ReplicationDataset(dataset: dataset, from: base, snapshots: [target])
    let header = try SendStream.replicationHeader(
      for: target, datasets: [selection], options: options)
    XCTAssertGreaterThan(header.count, try SendStream.compoundHeader(for: target).count)
    XCTAssertThrowsError(try SendStream.replicationHeader(for: target, datasets: []))
    XCTAssertThrowsError(
      try SendStream.replicationHeader(
        for: target, datasets: [selection, selection], options: options))
    let backwards = ReplicationDataset(dataset: dataset, from: target, snapshots: [base])
    XCTAssertThrowsError(
      try SendStream.replicationHeader(for: base, datasets: [backwards], options: options))
    let wrongOwner = ReplicationDataset(dataset: target, snapshots: [target])
    XCTAssertThrowsError(
      try SendStream.replicationHeader(for: target, datasets: [wrongOwner], options: options))

    // On a hierarchy fixture, include one child while omitting its siblings.
    var includedChild: Dataset?
    var excludedChild: Dataset?
    var hierarchyHeader: Data?
    let children = try dataset.children()
    if children.count >= 2 {
      let component = String(target.name.split(separator: "@").last!)
      let child = children[0]
      let childTarget = try child.snapshot(named: component)
      let childEntry = ReplicationDataset(dataset: child, snapshots: [childTarget])
      hierarchyHeader = try SendStream.replicationHeader(
        for: target, datasets: [selection, childEntry], options: options)
      includedChild = child
      excludedChild = children[1]
      XCTAssertThrowsError(
        try SendStream.replicationHeader(for: target, datasets: [childEntry], options: options))
    }

    func dumpHeader(_ header: Data) throws -> String {
      let file = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
      defer { try? FileManager.default.removeItem(at: file) }
      var package = header
      package.append(try SendStream.compoundFooter())
      try package.write(to: file)
      let input = try FileHandle(forReadingFrom: file)
      defer { try? input.close() }
      let output = Pipe()
      let dump = Process()
      dump.executableURL = URL(fileURLWithPath: "/usr/bin/env")
      dump.arguments = ["zstream", "dump"]
      dump.standardInput = input
      dump.standardOutput = output
      try dump.run()
      let text = String(decoding: output.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
      dump.waitUntilExit()
      XCTAssertEqual(dump.terminationReason, .exit)
      XCTAssertEqual(dump.terminationStatus, 0)
      return text
    }
    let text = try dumpHeader(hierarchyHeader ?? header)
    let retainingHistory = ReplicationDataset(
      dataset: dataset, from: base, snapshots: [target], retainedSnapshots: [omitted])
    let retainedHeader = try SendStream.replicationHeader(
      for: target, datasets: [retainingHistory], options: options)
    let retainedText = try dumpHeader(retainedHeader)
    XCTAssertTrue(retainedText.contains(String(omitted.name.split(separator: "@").last!)))
    XCTAssertTrue(text.contains("hdrtype = 2"))
    XCTAssertTrue(text.contains("fss = (embedded nvlist)"))
    XCTAssertTrue(text.contains("props = (embedded nvlist)"))
    XCTAssertTrue(text.contains("snapprops = (embedded nvlist)"))
    XCTAssertTrue(text.contains(base.name.split(separator: "@").last.map(String.init)!))
    XCTAssertTrue(text.contains(target.name.split(separator: "@").last.map(String.init)!))
    XCTAssertFalse(text.contains(omitted.name.split(separator: "@").last.map(String.init)!))
    if let includedChild, let excludedChild {
      XCTAssertTrue(text.contains("name = \(includedChild.name)"))
      XCTAssertFalse(text.contains("name = \(excludedChild.name)"))
      XCTAssertTrue(text.contains("parentfromsnap = 0x\(String(base.guid, radix: 16))"))
    }

  }

  /// Opt-in, read-only comparison with the native send package encoder.
  /// Choose a small snapshot range: both packages are written to temporary files.
  func testCompoundSendMatchesNative() async throws {
    let environment = ProcessInfo.processInfo.environment
    guard let targetName = environment["SWIFTZFS_TEST_SEND_SNAPSHOT"],
      let baseName = environment["SWIFTZFS_TEST_SEND_BASE"]
    else {
      throw XCTSkip("Set SWIFTZFS_TEST_SEND_SNAPSHOT and SWIFTZFS_TEST_SEND_BASE to a small range")
    }
    let poolName = String(
      targetName.split(separator: "/", maxSplits: 1)[0].split(separator: "@")[0])
    let pool = try ZFS().pool(named: poolName)
    let target = try XCTUnwrap(pool.dataset(named: targetName) as? Snapshot)
    let base = try XCTUnwrap(pool.dataset(named: baseName) as? Snapshot)
    let dataset = try XCTUnwrap(target.parent)
    XCTAssertEqual(base.parent, dataset)
    let snapshots = try dataset.snapshots()
    let start = try XCTUnwrap(snapshots.firstIndex(of: base))
    let end = try XCTUnwrap(snapshots.firstIndex(of: target))
    guard start < end else { throw XCTSkip("The target must be newer than the base") }

    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let actualURL = directory.appendingPathComponent("actual.zfs")
    let nativeURL = directory.appendingPathComponent("native.zfs")
    XCTAssertTrue(FileManager.default.createFile(atPath: actualURL.path, contents: nil))
    XCTAssertTrue(FileManager.default.createFile(atPath: nativeURL.path, contents: nil))
    let output = try FileHandle(forWritingTo: actualURL)
    defer { try? output.close() }
    try output.write(contentsOf: SendStream.compoundHeader(for: target))
    var previous = base
    let options = SendOptions(embeddedData: true, largeBlocks: true, compressed: true, raw: true)
    for snapshot in snapshots[(start + 1)...end] {
      for try await data in try snapshot.send(from: previous, options: options) {
        try output.write(contentsOf: data)
      }
      previous = snapshot
    }
    try output.write(contentsOf: SendStream.compoundFooter())
    try output.close()

    let nativeOutput = try FileHandle(forWritingTo: nativeURL)
    defer { try? nativeOutput.close() }
    let native = Process()
    native.executableURL = URL(fileURLWithPath: "/usr/bin/env")
    native.arguments = ["zfs", "send", "-w", "-I", base.name, target.name]
    native.standardOutput = nativeOutput
    try native.run()
    native.waitUntilExit()
    XCTAssertEqual(native.terminationReason, .exit)
    XCTAssertEqual(native.terminationStatus, 0)
    try nativeOutput.close()

    let actual = try FileHandle(forReadingFrom: actualURL)
    let expected = try FileHandle(forReadingFrom: nativeURL)
    defer {
      try? actual.close()
      try? expected.close()
    }
    while true {
      let block = try actual.read(upToCount: 128 * 1024) ?? Data()
      let reference = try expected.read(upToCount: 128 * 1024) ?? Data()
      guard block == reference else {
        XCTFail("Compound package differs from native zfs send -w -I")
        return
      }
      if block.isEmpty { break }
    }
  }

  func testSnapshotSend() async throws {
    guard let snapshotName = ProcessInfo.processInfo.environment["SWIFTZFS_TEST_SEND_SNAPSHOT"]
    else {
      throw XCTSkip("Set SWIFTZFS_TEST_SEND_SNAPSHOT to run a full send against a local snapshot")
    }

    let poolName = String(
      snapshotName.split(separator: "/", maxSplits: 1)[0].split(separator: "@")[0])
    let pool = try ZFS().pool(named: poolName)
    let snapshot = try XCTUnwrap(pool.dataset(named: snapshotName) as? Snapshot)

    var byteCount = 0
    let fullStream = try snapshot.send()
    XCTAssertTrue(fullStream.pool === snapshot.pool)
    XCTAssertTrue(fullStream.snapshot === snapshot)
    XCTAssertNil(fullStream.from)
    for try await data in fullStream {
      XCTAssertFalse(data.isEmpty)
      byteCount += data.count
    }
    XCTAssertGreaterThan(byteCount, 0)
    XCTAssertTrue(fullStream.snapshot === snapshot)
    XCTAssertTrue(fullStream.pool === snapshot.pool)

    var nilBaseByteCount = 0
    for try await data in try snapshot.send(from: nil) {
      XCTAssertFalse(data.isEmpty)
      nilBaseByteCount += data.count
    }
    XCTAssertEqual(nilBaseByteCount, byteCount)

    let output = try FileHandle(forWritingTo: URL(fileURLWithPath: "/dev/null"))
    defer { try? output.close() }
    try snapshot.send(toFileDescriptor: output.fileDescriptor, from: nil)

    if let baseName = ProcessInfo.processInfo.environment["SWIFTZFS_TEST_SEND_BASE"] {
      let base = try XCTUnwrap(pool.dataset(named: baseName) as? Snapshot)
      var incrementalBytes = 0
      let incrementalStream = try snapshot.send(from: base)
      XCTAssertTrue(incrementalStream.pool === snapshot.pool)
      XCTAssertTrue(incrementalStream.snapshot === snapshot)
      XCTAssertTrue(incrementalStream.from === base)
      for try await data in incrementalStream {
        XCTAssertFalse(data.isEmpty)
        incrementalBytes += data.count
      }
      XCTAssertGreaterThan(incrementalBytes, 0)
      XCTAssertTrue(incrementalStream.from === base)
    }
  }

  func testPoolDatasetLookup() throws {
    let zfs = try ZFS()
    guard let pool = try zfs.pools().first else {
      throw XCTSkip("No imported ZFS pool is available")
    }

    let root = try pool.dataset(named: pool.name)
    XCTAssertEqual(root.name, pool.name)
    XCTAssertTrue(root.isFilesystem)
    XCTAssertTrue(root.pool === pool)

    let child = try pool.children().first
    if let child {
      let opened = try pool.dataset(named: child.name)
      XCTAssertEqual(opened.name, child.name)
      XCTAssertTrue(child.pool === pool)
      XCTAssertTrue(opened.pool === pool)
      XCTAssertEqual(opened.guid, child.guid)
      XCTAssertEqual(opened.isVolume, child.isVolume)
      XCTAssertEqual(try opened.children().map(\.name), try child.children().map(\.name))
    }

    XCTAssertThrowsError(try pool.dataset(named: "\(pool.name)/swiftzfs-missing-\(UUID())")) {
      error in
      XCTAssertEqual((error as? ZFSError)?.code, .notFound)
    }
    XCTAssertThrowsError(try pool.dataset(named: "otherpool/dataset")) { error in
      XCTAssertEqual((error as? ZFSError)?.code, .invalidArgument)
    }
  }

  func testPoolLookupAndImportAreSeparate() throws {
    let zfs = try ZFS()
    guard let existing = try zfs.pools().first else {
      throw XCTSkip("No imported ZFS pool is available")
    }

    let opened = try zfs.pool(named: existing.name)
    XCTAssertEqual(opened.name, existing.name)
    XCTAssertEqual(opened.guid, existing.guid)

    XCTAssertThrowsError(try zfs.importPool(named: existing.name)) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .alreadyExists = error.code else {
        XCTFail("Expected .alreadyExists, got \(error.code)")
        return
      }
    }

    for guid in [String(existing.guid), "0x" + String(existing.guid, radix: 16)] {
      XCTAssertThrowsError(try zfs.importPool(guid: guid)) { error in
        guard let error = error as? ZFSError else {
          XCTFail("Expected ZFSError, got \(error)")
          return
        }
        guard case .alreadyExists = error.code else {
          XCTFail("Expected .alreadyExists, got \(error.code)")
          return
        }
      }
    }
    XCTAssertThrowsError(try zfs.importPool(guid: existing.guid)) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .alreadyExists = error.code else {
        XCTFail("Expected .alreadyExists, got \(error.code)")
        return
      }
    }

    XCTAssertThrowsError(try zfs.pool(named: "swiftzfs-missing-\(UUID())")) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .noSuchPool = error.code else {
        XCTFail("Expected .noSuchPool, got \(error.code)")
        return
      }
    }
  }

  func testImportPoolRejectsEmptyName() throws {
    let zfs = try ZFS()
    XCTAssertThrowsError(try zfs.importPool(named: "")) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .invalidArgument = error.code else {
        XCTFail("Expected .invalidArgument, got \(error.code)")
        return
      }
    }

    XCTAssertThrowsError(try zfs.importPool(guid: "not-a-guid")) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .invalidArgument = error.code else {
        XCTFail("Expected .invalidArgument, got \(error.code)")
        return
      }
    }
    XCTAssertThrowsError(try zfs.importPool(guid: GUID(0))) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .invalidArgument = error.code else {
        XCTFail("Expected .invalidArgument, got \(error.code)")
        return
      }
    }
  }

  func testImportMissingPoolByName() throws {
    guard ProcessInfo.processInfo.environment["SWIFTZFS_TEST_IMPORT_SEARCH"] == "1" else {
      throw XCTSkip("Set SWIFTZFS_TEST_IMPORT_SEARCH=1 to scan local devices")
    }
    let zfs = try ZFS()
    XCTAssertThrowsError(try zfs.importPool(named: "swiftzfs-missing-\(UUID())")) { error in
      guard let error = error as? ZFSError else {
        XCTFail("Expected ZFSError, got \(error)")
        return
      }
      guard case .noSuchPool = error.code else {
        XCTFail("Expected .noSuchPool, got \(error.code)")
        return
      }
    }
  }
}
