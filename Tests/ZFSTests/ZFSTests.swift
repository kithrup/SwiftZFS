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
}
