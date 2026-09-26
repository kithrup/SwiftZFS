import Foundation
import ZFS

@main
struct ListSnapshots {
  static func main() {
    guard CommandLine.arguments.count == 2 else {
      FileHandle.standardError.write(Data("Usage: list-snapshots <dataset>\n".utf8))
      exit(2)
    }

    let datasetName = CommandLine.arguments[1]
    let poolName = String(datasetName.prefix { $0 != "/" })
    do {
      guard let pool = try ZFS().pools().first(where: { $0.name == poolName }) else {
        FileHandle.standardError.write(Data("Pool not found: \(poolName)\n".utf8))
        exit(1)
      }

      let datasets = try pool.datasets()
      var selected: Dataset?
      for filesystem in datasets.filesystems where filesystem.name == datasetName {
        selected = filesystem
        break
      }
      if selected == nil {
        for volume in datasets.volumes where volume.name == datasetName {
          selected = volume
          break
        }
      }
      guard let selected else {
        FileHandle.standardError.write(Data("Dataset not found: \(datasetName)\n".utf8))
        exit(1)
      }

      for snapshot in try selected.snapshots() {
        print(snapshot.name)
      }
    } catch {
      FileHandle.standardError.write(Data("list-snapshots: \(error)\n".utf8))
      exit(1)
    }
  }
}
