import ZFS

@main
struct ZFSExample {
    static func main() {
        do {
            let zfs = try ZFS()
            let pools = try zfs.pools()

            if pools.isEmpty {
                print("No ZFS pools found.")
            } else {
                func printDataset(_ dataset: Dataset, indent: String) throws {
                    print("\(indent)\(dataset.name)")

                    for child in try dataset.children() {
                        try printDataset(child, indent: indent + "  ")
                    }
                }

                for pool in pools {
                    print(pool.name)

                    for child in try pool.children() {
                        try printDataset(child, indent: "  ")
                    }
                }
            }
        } catch {
            print("SwiftZFS error: \(error)")
        }
    }
}
