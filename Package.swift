// swift-tools-version: 6.0

import PackageDescription

let package = Package(
  name: "SwiftZFS",
  products: [
    .library(name: "ZFS", targets: ["ZFS"]),
    .library(name: "ZFSCPP", targets: ["ZFSCPP"]),
    .executable(name: "zfs-example", targets: ["zfs-example"]),
  ],
  targets: [
    // The C++ implementation is installed separately so this package can be
    // used as a versioned SwiftPM dependency without unsafe build flags.
    .systemLibrary(name: "ZFSCPP", path: "Sources/ZFSCPPSystem", pkgConfig: "swiftzfs-cpp"),
    .target(
      name: "CZFS",
      dependencies: ["ZFSCPP"],
      path: "Sources/CZFS",
      publicHeadersPath: "include"
    ),
    .target(
      name: "ZFS",
      dependencies: ["CZFS"]
    ),
    .executableTarget(
      name: "zfs-example",
      dependencies: ["ZFS"]
    ),
    .testTarget(
      name: "ZFSTests",
      dependencies: ["ZFS"]
    ),
  ],
  cxxLanguageStandard: .cxx17
)
