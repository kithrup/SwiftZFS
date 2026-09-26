import Foundation
import ZFS

@main
struct ListPools {
  static func main() {
    guard CommandLine.arguments.count == 1 else {
      FileHandle.standardError.write(Data("Usage: list-pools\n".utf8))
      exit(2)
    }

    do {
      for pool in try ZFS().pools() {
        print(pool.name)
      }
    } catch {
      FileHandle.standardError.write(Data("list-pools: \(error)\n".utf8))
      exit(1)
    }
  }
}
