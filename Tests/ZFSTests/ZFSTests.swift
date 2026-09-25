import XCTest
@testable import ZFS

final class ZFSTests: XCTestCase {
    func testZPoolValueType() {
        let pool = ZPool(name: "tank")
        XCTAssertEqual(pool.name, "tank")
    }
}
