import Foundation
import XCTest

@testable import ZFS

final class ZFSTests: XCTestCase {
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

    let openedSnapshot = try pool.dataset(named: snapshot.name)
    XCTAssertEqual(openedSnapshot.name, snapshot.name)
    XCTAssertTrue(openedSnapshot.isSnapshot)

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
