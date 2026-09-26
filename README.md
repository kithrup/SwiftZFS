# SwiftZFS

SwiftZFS provides a C++17 interface to OpenZFS and a Swift interface built on
that C++ layer through a C ABI. The Swift package exports two library products:
`ZFSCPP` for C++ targets and `ZFS` for Swift targets.

## Build

Run `./autogen.sh` if the `configure` script has not been generated, then run
`./configure`. The configure script locates the OpenZFS headers and libraries.
Use `--with-zfs-includedir` and `--with-zfs-libdir` for nonstandard locations.

``` tcsh
./configure
gmake cpp-build
gmake build
gmake test
```

Use GNU make (`gmake`) on FreeBSD. The C++ build uses C++17 with strict
warnings. `gmake docs` builds the C++ API reference when Doxygen is installed.

## C++ applications

`gmake cpp-build` produces `.build-cpp/libswiftzfs_cpp.a`. To install the
archive, public headers, and pkg-config metadata, configure an installation
prefix and run `gmake cpp-install`:

``` tcsh
./configure --prefix=/path/to/prefix
gmake cpp-install
```

`DESTDIR` can stage the installation. Installed C++ applications can include
`<swiftzfs/ZFS.hpp>` and obtain compiler and linker flags with
`pkg-config --cflags --libs swiftzfs-cpp`. Use a C++ compiler driver for the
link step. SwiftPM C++ targets can instead depend on the `ZFSCPP` product.

## Swift applications

Add this package as a SwiftPM dependency and depend on its `ZFS` product:

``` swift
dependencies: [.package(path: "/path/to/SwiftZFS")],
targets: [
    .executableTarget(name: "MyApp", dependencies: [
        .product(name: "ZFS", package: "SwiftZFS")
    ])
]
```

On FreeBSD, external SwiftPM builds need the `SWIFTZFS_CXXFLAGS` and
`SWIFTZFS_LINKER_FLAGS` values from the configured `Makefile` in their
environment. These flags supply the OpenZFS source compatibility headers and
any custom library path.

## Examples

After `./configure`, run `gmake cpp-examples` and `gmake swift-examples` from
the repository root. See [examples/README.md](examples/README.md) for the
binary paths and command-line arguments.
