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
                for pool in pools {
                    print(pool.name)
                }
            }
        } catch {
            print("SwiftZFS error: \(error)")
        }
    }
}
