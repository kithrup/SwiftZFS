import Foundation
import XCTest

@testable import ZFS

final class ZFSTests: XCTestCase {
  func testDatasetParentsAndPaths() throws {
    guard let pool = try ZFS().pools().first else { throw XCTSkip("No imported pool") }
    let datasets = try pool.datasets()
    let root = try XCTUnwrap(datasets.filesystems.first { $0.name == pool.name })
    XCTAssertNil(root.parent)
    var seen = Set<String>()
    func check(_ dataset: Dataset) throws {
      XCTAssertTrue(seen.insert(dataset.path).inserted)
      XCTAssertEqual(dataset.path, dataset.name)
      if dataset.name != pool.name {
        let expected = String(dataset.name.prefix(upTo: dataset.name.lastIndex(of: "/")!))
        XCTAssertEqual(dataset.parent?.path, expected)
      }
      for child in try dataset.children() {
        XCTAssertTrue(child.parent === dataset)
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

    let openedSnapshot = try pool.dataset(named: snapshot.name)
    XCTAssertEqual(openedSnapshot.name, snapshot.name)
    XCTAssertTrue(openedSnapshot.isSnapshot)
    XCTAssertEqual(openedSnapshot.parent?.path, dataset.path)

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
    for try await data in try snapshot.send() {
      XCTAssertFalse(data.isEmpty)
      byteCount += data.count
    }
    XCTAssertGreaterThan(byteCount, 0)

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
      for try await data in try snapshot.send(from: base) {
        XCTAssertFalse(data.isEmpty)
        incrementalBytes += data.count
      }
      XCTAssertGreaterThan(incrementalBytes, 0)
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

    let child = try pool.children().first
    if let child {
      let opened = try pool.dataset(named: child.name)
      XCTAssertEqual(opened.name, child.name)
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
