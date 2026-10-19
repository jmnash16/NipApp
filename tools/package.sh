#!/bin/bash
# Builds a NippApp tarball that runs on other x86-64 Linux PCs without building there:
#
#   nippapp-<version>/bin/nippapp     optimised build, looking in ../lib first (RPATH)
#   nippapp-<version>/lib/            the libraries whose versions differ between distributions
#   nippapp-<version>/player/         the web player
#   nippapp-<version>/sources.conf    this machine's sources file, to edit on the target
#   nippapp-<version>/README.txt
#
# Not bundled, so taken from the target:
#   - glibc (libc, libm, ...): the target needs this machine's glibc version or later, so build
#     the package on the oldest distribution it should run on;
#   - libavahi-client/libavahi-common, with libdbus and libsystemd: they have to match the
#     target's avahi-daemon. Package with MDNS=0 for a target without them;
#   - libz, liblzma, libstdc++, libgcc_s: present on every system, and backwards compatible, while
#     the system's own libraries may need the newer ones.
#
# Usage: tools/package.sh [make variables, e.g. MDNS=0]
# The tarball is written to build/. DVBCore must have been built (see DESIGN.md).

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build/package"
VERSION=$(sed -n 's/^#define NIPPAPP_VERSION *"\(.*\)"/\1/p' "$ROOT/inc/nippapp.h")
NAME="nippapp-$VERSION"
STAGE="$BUILD/$NAME"
TARBALL="$ROOT/build/$NAME-linux-$(uname -m).tar.gz"

# Libraries left to the target (see above), as names without the version
SYSTEM_LIBS='^(linux-vdso|ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libz|liblzma|libstdc\+\+|libgcc_s|libavahi-.*|libdbus-1|libsystemd|libcap|libgcrypt|libgpg-error|liblz4|libzstd)$'

echo "Building $NAME"
make -C "$ROOT" BUILD_DIR="$BUILD/obj-build" DEBUG=0 "$@" -j"$(nproc)" >/dev/null

rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib"
cp "$BUILD/obj-build/nippapp" "$STAGE/bin/"
strip "$STAGE/bin/nippapp"
cp -r "$ROOT/player" "$STAGE/player"
cp "$ROOT/sources.conf" "$STAGE/sources.conf"

# Every library the executable loads, directly or through another library, unless it is left to
# the target. ldd gives the libraries' real files; they are copied under their soname.
ldd "$STAGE/bin/nippapp" | while read -r soname arrow path rest; do
   [ "$arrow" = "=>" ] && [ -f "$path" ] || continue
   base=${soname%%.so*}
   if [[ ! $base =~ $SYSTEM_LIBS ]]; then
      cp -L "$path" "$STAGE/lib/$soname"
      echo "  bundled $soname"
   fi
done

GLIBC=$(objdump -T "$STAGE/bin/nippapp" "$STAGE"/lib/*.so* 2>/dev/null |
   grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1 | sed 's/GLIBC_//')
SYSTEM=$(ldd "$STAGE/bin/nippapp" | awk '$2 == "=>" { print $1 }' |
   while read -r soname; do [ -f "$STAGE/lib/$soname" ] || echo "$soname"; done | sort | tr '\n' ' ')

cat >"$STAGE/README.txt" <<EOF
NippApp $VERSION: DVB-NIP gateway (built $(date +%Y-%m-%d) on $(lsb_release -ds 2>/dev/null || uname -sr))

Needs: x86-64 Linux with glibc $GLIBC or later (Ubuntu 24.04 or later), and these system
libraries, which every Ubuntu desktop has:
  $SYSTEM
On a server install, add Avahi for the mDNS announcement:
  sudo apt install libavahi-client3 avahi-daemon

Run, from this directory:
  bin/nippapp                      uses ./sources.conf, HTTP on port 8080
  bin/nippapp --help               the options
The player is at http://<this PC>:8080/player/ and the DVB-I entry points at
http://<this PC>:8080/dvbi/slep.xml.

sources.conf names the captures that stand in for the carriers: copy the captures to this PC and
edit their paths (relative paths are relative to sources.conf). NippApp keeps its database and
received files in the directory it is started in, or the one given with -d.

The player is in player/; bin/nippapp finds it there, or give another with -P.
The libraries in lib/ are found through the executable's RPATH (\$ORIGIN/../lib); they are
copies of this machine's, under their own licences (libxml2: MIT, ICU: Unicode, SQLite: public
domain, libjpeg-turbo: BSD-style/IJG, libpng: libpng).
EOF

tar -C "$BUILD" -czf "$TARBALL" "$NAME"
echo "Package: $TARBALL ($(du -h "$TARBALL" | cut -f1)), needs glibc $GLIBC or later"
