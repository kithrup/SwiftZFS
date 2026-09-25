AC_PREREQ([2.69])
AC_INIT([SwiftZFS], [0.6.0], [none])
AC_CONFIG_SRCDIR([Package.swift])
AC_CANONICAL_HOST
AC_PROG_CC
AC_PROG_CXX

AC_ARG_WITH(
    [zfs-includedir],
    [AS_HELP_STRING([--with-zfs-includedir=DIR],
        [directory containing installed libzfs.h, or an OpenZFS source-tree include directory])],
    [ZFS_INCLUDEDIR="$withval"],
    [ZFS_INCLUDEDIR=""])

AC_ARG_WITH(
    [zfs-libdir],
    [AS_HELP_STRING([--with-zfs-libdir=DIR],
        [directory containing OpenZFS libraries])],
    [ZFS_LIBDIR="$withval"],
    [ZFS_LIBDIR=""])

AC_ARG_WITH(
    [zfs-source],
    [AS_HELP_STRING([--with-zfs-source=DIR],
        [OpenZFS source-tree root (normally inferred from --with-zfs-includedir)])],
    [ZFS_SOURCE_DIR="$withval"],
    [ZFS_SOURCE_DIR=""])

AC_ARG_WITH(
    [zfs-build],
    [AS_HELP_STRING([--with-zfs-build=DIR],
        [configured OpenZFS build-tree root; defaults to source root for an in-tree build])],
    [ZFS_BUILD_DIR="$withval"],
    [ZFS_BUILD_DIR=""])

# Canonicalize user-supplied directories early.  This also makes the generated
# Swift compiler flags independent of the directory from which make is run.
for swiftzfs_var in ZFS_INCLUDEDIR ZFS_LIBDIR ZFS_SOURCE_DIR ZFS_BUILD_DIR; do
    eval swiftzfs_val=\$$swiftzfs_var
    if test -n "$swiftzfs_val"; then
        if test ! -d "$swiftzfs_val"; then
            AC_MSG_ERROR([$swiftzfs_var directory does not exist: $swiftzfs_val])
        fi
        swiftzfs_val=`cd "$swiftzfs_val" && pwd`
        eval $swiftzfs_var=\$swiftzfs_val
    fi
done

# If --with-zfs-includedir names <openzfs>/include, recognize that this is a
# source checkout rather than an installed header directory.  OpenZFS's
# source-tree headers require more than -Iinclude; Rules.am force-includes
# zfs_config.h and sys/simd_config.h and adds libspl/libzpool include paths.
if test -z "$ZFS_SOURCE_DIR" && test -n "$ZFS_INCLUDEDIR"; then
    swiftzfs_candidate=`dirname "$ZFS_INCLUDEDIR"`
    if test -f "$swiftzfs_candidate/configure.ac" && \
       test -d "$swiftzfs_candidate/lib/libspl/include"; then
        ZFS_SOURCE_DIR="$swiftzfs_candidate"
    fi
fi

if test -n "$ZFS_SOURCE_DIR" && test -z "$ZFS_BUILD_DIR"; then
    if test -f "$ZFS_SOURCE_DIR/zfs_config.h"; then
        ZFS_BUILD_DIR="$ZFS_SOURCE_DIR"
    fi
fi

if test -n "$ZFS_SOURCE_DIR" && test -z "$ZFS_INCLUDEDIR"; then
    ZFS_INCLUDEDIR="$ZFS_SOURCE_DIR/include"
fi

# Current OpenZFS uses a non-recursive Automake build, so an in-tree build
# normally places libtool outputs in <build>/.libs.  Infer that location when
# possible, while still allowing --with-zfs-libdir to override it.
if test -z "$ZFS_LIBDIR" && test -n "$ZFS_BUILD_DIR" && \
   test -d "$ZFS_BUILD_DIR/.libs"; then
    ZFS_LIBDIR="$ZFS_BUILD_DIR/.libs"
fi

ZFS_CPPFLAGS=""
SWIFT_LINKER_FLAGS=""

if test -n "$ZFS_SOURCE_DIR"; then
    if test -z "$ZFS_BUILD_DIR"; then
        AC_MSG_ERROR([OpenZFS source tree detected at $ZFS_SOURCE_DIR, but zfs_config.h was not found. Run OpenZFS ./configure first, or specify --with-zfs-build=DIR for an out-of-tree build.])
    fi
    if test ! -f "$ZFS_BUILD_DIR/zfs_config.h"; then
        AC_MSG_ERROR([OpenZFS build tree $ZFS_BUILD_DIR does not contain zfs_config.h])
    fi
    if test ! -f "$ZFS_SOURCE_DIR/include/sys/simd_config.h"; then
        AC_MSG_ERROR([OpenZFS source tree $ZFS_SOURCE_DIR does not contain include/sys/simd_config.h])
    fi

    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -include $ZFS_BUILD_DIR/zfs_config.h"
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -include $ZFS_SOURCE_DIR/include/sys/simd_config.h"
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -I$ZFS_BUILD_DIR/include"
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -I$ZFS_SOURCE_DIR/include"
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -I$ZFS_SOURCE_DIR/lib/libspl/include"
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -I$ZFS_SOURCE_DIR/lib/libspl/include/os/freebsd"
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -I$ZFS_SOURCE_DIR/lib/libzpool/include"
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -D_GNU_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE"

    case "$host_os" in
        freebsd*)
            if test -f "$ZFS_SOURCE_DIR/include/os/freebsd/spl/sys/ccompile.h"; then
                ZFS_CPPFLAGS="$ZFS_CPPFLAGS -include $ZFS_SOURCE_DIR/include/os/freebsd/spl/sys/ccompile.h"
            fi
            ;;
    esac


elif test -n "$ZFS_INCLUDEDIR"; then
    ZFS_CPPFLAGS="$ZFS_CPPFLAGS -I$ZFS_INCLUDEDIR"
fi

if test -n "$ZFS_LIBDIR"; then
    LDFLAGS="$LDFLAGS -L$ZFS_LIBDIR"
    SWIFT_LINKER_FLAGS="$SWIFT_LINKER_FLAGS -L$ZFS_LIBDIR"
    # This is particularly useful when testing against an uninstalled
    # OpenZFS libtool build directory.
    SWIFT_LINKER_FLAGS="$SWIFT_LINKER_FLAGS -Xlinker -rpath -Xlinker $ZFS_LIBDIR"
fi

swiftzfs_save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $ZFS_CPPFLAGS"

AC_LANG_PUSH([C++])
AC_CHECK_HEADERS(
    [libzfs.h libzfs_core.h],
    [],
    [AC_MSG_ERROR([required OpenZFS header could not be compiled. See config.log; for a source checkout, point --with-zfs-includedir at its include directory after OpenZFS has been configured.])])
AC_LANG_POP([C++])

# Restore the caller's CPPFLAGS.  The OpenZFS-specific flags are exported to Package.swift for the CZFS C++ target only.
CPPFLAGS="$swiftzfs_save_CPPFLAGS"

# The skeleton currently calls only libzfs.  Probe libzfs as required; probe
# libzfs_core and libzpool as future capabilities without making this first
# pool-listing smoke test depend on their link closure.
AC_CHECK_LIB(
    [zfs],
    [libzfs_init],
    [HAVE_LIBZFS=yes],
    [AC_MSG_ERROR([libzfs not found or not linkable; use --with-zfs-libdir if necessary])])

AC_CHECK_LIB(
    [nvpair],
    [nvlist_lookup_nvlist],
    [HAVE_LIBNVPAIR=yes],
    [AC_MSG_ERROR([libnvpair not found or not linkable; it is required for vdev configuration parsing])])

swiftzfs_save_LIBS="$LIBS"
AC_CHECK_LIB([zfs_core], [lzc_exists], [HAVE_LIBZFS_CORE=yes], [HAVE_LIBZFS_CORE=no])
LIBS="$swiftzfs_save_LIBS"

AC_CHECK_LIB([zpool], [libzpool_init], [HAVE_LIBZPOOL=yes], [HAVE_LIBZPOOL=no])
LIBS="$swiftzfs_save_LIBS"

AS_IF([test "x$HAVE_LIBZFS_CORE" != "xyes"], [
    AC_MSG_WARN([libzfs_core probe failed; the current skeleton does not use it yet])
])
AS_IF([test "x$HAVE_LIBZPOOL" != "xyes"], [
    AC_MSG_WARN([libzpool probe failed; the current skeleton does not use it yet])
])

AC_SUBST([ZFS_CPPFLAGS])
AC_SUBST([SWIFT_LINKER_FLAGS])
AC_SUBST([ZFS_INCLUDEDIR])
AC_SUBST([ZFS_LIBDIR])
AC_SUBST([ZFS_SOURCE_DIR])
AC_SUBST([ZFS_BUILD_DIR])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_NOTICE([SwiftZFS configuration:])
AC_MSG_NOTICE([  host:               $host])
AC_MSG_NOTICE([  ZFS source dir:     ${ZFS_SOURCE_DIR:-not a source-tree build}])
AC_MSG_NOTICE([  ZFS build dir:      ${ZFS_BUILD_DIR:-not a source-tree build}])
AC_MSG_NOTICE([  ZFS include dir:    ${ZFS_INCLUDEDIR:-system default}])
AC_MSG_NOTICE([  ZFS library dir:    ${ZFS_LIBDIR:-system default}])
AC_MSG_NOTICE([  libnvpair:          $HAVE_LIBNVPAIR])
AC_MSG_NOTICE([  libzfs_core:        $HAVE_LIBZFS_CORE])
AC_MSG_NOTICE([  libzpool:           $HAVE_LIBZPOOL])
AC_MSG_NOTICE([  CZFS C++ flags:      $ZFS_CPPFLAGS])
AC_MSG_NOTICE([  CZFS linker flags:   $SWIFT_LINKER_FLAGS])
