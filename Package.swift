// swift-tools-version: 6.0

import Foundation
import PackageDescription

private func environmentFlags(_ name: String) -> [String] {
    guard let value = ProcessInfo.processInfo.environment[name], !value.isEmpty else {
        return []
    }
    return value.split(whereSeparator: { $0.isWhitespace }).map(String.init)
}

// configure/Makefile pass these only through the manifest environment.  In
// particular, the OpenZFS -include/-I/-D options must not be supplied via
// swift(1)'s global -Xcc option: doing that also applies them while Swift is
// building its own Clang modules (e.g. SwiftGlibc on FreeBSD).
let zfsCXXFlags = environmentFlags("SWIFTZFS_CXXFLAGS")
let zfsLinkerFlags = environmentFlags("SWIFTZFS_LINKER_FLAGS")

let package = Package(
    name: "SwiftZFS",
    products: [
        .library(name: "ZFS", targets: ["ZFS"]),
        .executable(name: "zfs-example", targets: ["zfs-example"]),
    ],
    targets: [
        .target(
            name: "ZFSCPP",
            path: "Sources/ZFSCPP",
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("internal"),
                .define("__STDC_LIMIT_MACROS"),
                .define("__STDC_CONSTANT_MACROS"),
                .unsafeFlags(zfsCXXFlags),
            ],
            linkerSettings: [
                .linkedLibrary("zfs"),
                .linkedLibrary("zfs_core"),
                .linkedLibrary("nvpair"),
                .linkedLibrary("pthread"),
                .linkedLibrary("m"),
                .unsafeFlags(zfsLinkerFlags),
            ]
        ),
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
