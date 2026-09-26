// swift-tools-version: 6.0

import PackageDescription

let package = Package(
  name: "SwiftZFSExamples",
  dependencies: [.package(path: "../..")],
  targets: [
    .executableTarget(
      name: "list-pools",
      dependencies: [.product(name: "ZFS", package: "SwiftZFS")]
    ),
    .executableTarget(
      name: "list-datasets",
      dependencies: [.product(name: "ZFS", package: "SwiftZFS")]
    ),
    .executableTarget(
      name: "list-snapshots",
      dependencies: [.product(name: "ZFS", package: "SwiftZFS")]
    ),
  ]
)
