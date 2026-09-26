# Examples

Both directories contain three read-only programs:

| Program | Argument | Output |
| --- | --- | --- |
| `list-pools` | None | Imported pool names |
| `list-datasets` | Pool name | Filesystem and volume names, including the pool root |
| `list-snapshots` | Dataset name | Snapshots belonging directly to that filesystem or volume |

## Build from the repository root

From a fresh checkout, generate the configure script, configure OpenZFS paths,
and build both sets of examples:

``` tcsh
./autogen.sh
./configure
gmake cpp-examples
gmake swift-examples
```

The configure step accepts `--with-zfs-includedir` and `--with-zfs-libdir` for
nonstandard OpenZFS locations. Use `gmake` on FreeBSD.

## Run

The C++ binaries are under `.build-cpp/examples`; the Swift binaries are under
`examples/Swift/.build/debug`:

``` tcsh
.build-cpp/examples/list-pools
.build-cpp/examples/list-datasets zroot
.build-cpp/examples/list-snapshots zroot/ROOT

examples/Swift/.build/debug/list-pools
examples/Swift/.build/debug/list-datasets zroot
examples/Swift/.build/debug/list-snapshots zroot/ROOT
```

Replace `zroot` and `zroot/ROOT` with names on your system. The Swift examples
form a separate SwiftPM package that depends on this repository's `ZFS`
product. `gmake swift-examples` passes the configured OpenZFS flags to that
package on FreeBSD.

To build the C++ examples against an installed SwiftZFS library instead,
run `gmake -C examples/C++`. That route uses `pkg-config` and requires
`swiftzfs-cpp` to be installed first.
