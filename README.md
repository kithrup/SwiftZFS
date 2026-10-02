# SwiftZFS

SwiftZFS provides a C++17 interface to OpenZFS and a Swift interface built on
that C++ layer through a C ABI. The Swift package exports two library products:
`ZFSCPP` for C++ targets and `ZFS` for Swift targets. SwiftPM uses the installed
C++ library through `pkg-config`; GNU make builds that library from source.

## Build

Run `./autogen.sh` if the `configure` script has not been generated, then run
`./configure`. The configure script locates the OpenZFS headers and libraries.
Swift is detected automatically. Use `--with-swift` to require a working
Swift toolchain and fail configuration if it is unavailable.
If the Swift toolchain is unavailable, or `--without-swift` is specified,
the default build includes only the C++ interface. In this mode, `gmake build`,
`gmake test`, and `gmake run` use the C++ targets, `gmake docs` builds only the
C++ reference, and `gmake clean` does not invoke Swift. The explicit
`c-shim-*` targets remain available.
Use `--with-zfs-includedir` and `--with-zfs-libdir` for nonstandard locations.

``` tcsh
./configure
gmake cpp-build
gmake build
gmake test
```

Use GNU make (`gmake`) on FreeBSD. The C++ build uses C++17 with strict
warnings. `gmake docs` builds the C++ API reference with Doxygen and the Swift
API reference with DocC.

`gmake build`, `gmake test`, and the other Swift targets first build the C++
archive and stage its public headers locally. They set `PKG_CONFIG_PATH` for
SwiftPM automatically; a system-wide installation is not needed for development.
For direct `swift build` commands, first run `gmake swift-support` and set
`PKG_CONFIG_PATH` to the checkout's `.build-cpp/pkgconfig` directory.

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

First build and install the C++ library from the same SwiftZFS revision as
your package dependency. For example, install it under your home directory:

``` tcsh
./configure --prefix=$HOME/.local
gmake cpp-install
setenv PKG_CONFIG_PATH "$HOME/.local/lib/pkgconfig"
pkg-config --cflags --libs swiftzfs-cpp
```

If `PKG_CONFIG_PATH` is already set, prepend the new directory to its existing
value. FreeBSD also needs its base-system OpenZFS sources under
`/usr/src/sys/contrib/openzfs`, or an alternate tree selected with configure.
Linux needs the OpenZFS development headers and libraries.

Then add this package as a SwiftPM dependency and depend on its `ZFS` product:

``` swift
dependencies: [.package(path: "/path/to/SwiftZFS")],
targets: [
    .executableTarget(name: "MyApp", dependencies: [
        .product(name: "ZFS", package: "SwiftZFS")
    ])
]
```

Git URL dependencies use the same setup as the local path dependency above.
Use a revision containing the system-library manifest; older releases such as
`0.1.1` still try to compile OpenZFS-dependent C++ sources inside SwiftPM.

SwiftPM builds the C shim and Swift interface, linking the installed C++ archive.
Only its public headers enter SwiftPM's header search paths. OpenZFS compatibility
headers stay in the GNU make build, and the package requires no `unsafeFlags`
that would prevent use as a versioned dependency. The old
`SWIFTZFS_CXXFLAGS` and `SWIFTZFS_LINKER_FLAGS` environment variables are no
longer used by the package manifest.

The Swift API is documented in source comments and the
[ZFS documentation catalog](Sources/ZFS/ZFS.docc/ZFS.md). For example, open a
dataset by its full name without manually searching `pool.datasets()`:

```swift
import ZFS

let zfs = try ZFS()
let pool = try zfs.pool(named: "tank")
let dataset = try pool.dataset(named: "tank/home")
print(dataset.name)
```

Run `gmake docs` to generate both API references. The C++ HTML reference is
written to `docs/html/`, and the Swift DocC archive to `docs/swift.doccarchive/`.
Use `gmake cpp-docs` or `gmake swift-docs` to build either reference separately.

## Examples

After `./configure`, run `gmake cpp-examples` and `gmake swift-examples` from
the repository root. See [examples/README.md](examples/README.md) for the
binary paths and command-line arguments.
