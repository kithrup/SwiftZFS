import Foundation
import ZFS

@main
struct ListDatasets {
  static func main() {
    guard CommandLine.arguments.count == 2 else {
      FileHandle.standardError.write(Data("Usage: list-datasets <pool>\n".utf8))
      exit(2)
    }

    let poolName = CommandLine.arguments[1]
    do {
      guard let pool = try ZFS().pools().first(where: { $0.name == poolName }) else {
        FileHandle.standardError.write(Data("Pool not found: \(poolName)\n".utf8))
        exit(1)
      }

      let datasets = try pool.datasets()
      for filesystem in datasets.filesystems {
        print(filesystem.name)
      }
      for volume in datasets.volumes {
        print(volume.name)
      }
    } catch {
      FileHandle.standardError.write(Data("list-datasets: \(error)\n".utf8))
      exit(1)
    }
  }
}
