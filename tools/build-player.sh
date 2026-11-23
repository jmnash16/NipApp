#!/bin/bash
###############################################################################
# Copyright © 2026 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
#
# This file is part of a DTVKit Software Component
# You are permitted to copy, modify or distribute this file subject to the terms
# of the DTVKit 1.0 Licence which can be found in licence.txt or at www.dtvkit.org
#
# THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
# EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
#
# If you or your organisation is not a member of DTVKit then you have access
# to this source code outside of the terms of the licence agreement
# and you are expected to delete this and any associated files immediately.
# Further information on DTVKit, membership and terms can be found at www.dtvkit.org
###############################################################################
#
# Builds the files the web player needs into player/vendor/ (not kept in git):
#   - dvbi-client-js, built from its checkout (DVBI_CLIENT_JS, default ../../dvbi-client-js
#     relative to the NippApp directory's parent, i.e. ~/dvbi-client-js)
#   - dash.js and hls.js, pinned versions from npm, with their licences
#
# Needs Node.js 20.19 or newer, and access to the npm registry the first time.
#
#   tools/build-player.sh

set -euo pipefail

DASHJS_VERSION=4.7.4
HLSJS_VERSION=1.5.20

nippapp_dir="$(cd "$(dirname "$0")/.." && pwd)"
client_dir="${DVBI_CLIENT_JS:-$nippapp_dir/../../dvbi-client-js}"
vendor_dir="$nippapp_dir/player/vendor"

if [ ! -f "$client_dir/package.json" ]; then
   echo "dvbi-client-js not found at $client_dir; set DVBI_CLIENT_JS" >&2
   exit 1
fi
client_dir="$(cd "$client_dir" && pwd)"

echo "Building dvbi-client-js in $client_dir"
(
   cd "$client_dir"
   if [ ! -d node_modules ]; then
      # Install scripts are not needed to build the library
      npm ci --ignore-scripts --no-audit --no-fund
   fi
   npx vite build
)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "Fetching dash.js $DASHJS_VERSION and hls.js $HLSJS_VERSION"
(cd "$work" && npm pack --silent "dashjs@$DASHJS_VERSION" "hls.js@$HLSJS_VERSION" > /dev/null)
mkdir -p "$work/dashjs" "$work/hlsjs"
tar -xzf "$work/dashjs-$DASHJS_VERSION.tgz" -C "$work/dashjs"
tar -xzf "$work/hls.js-$HLSJS_VERSION.tgz" -C "$work/hlsjs"

rm -rf "$vendor_dir"
mkdir -p "$vendor_dir/dvbi-client-js" "$vendor_dir/dashjs" "$vendor_dir/hlsjs"

cp -R "$client_dir/dist/." "$vendor_dir/dvbi-client-js/"
# Not its source maps: they contain dvbi-client-js's full source, which the gateway would serve to
# any browser, and DTVKit source is for DTVKit members only
find "$vendor_dir/dvbi-client-js" -name '*.map' -delete
find "$vendor_dir/dvbi-client-js" -name '*.js' -exec sed -i '/^\/\/# sourceMappingURL=/d' {} +
# dvbi-client-js's own licence.txt is a placeholder: the licence text is NippApp's copy
cp "$nippapp_dir/licence.txt" "$vendor_dir/dvbi-client-js/"
git -C "$client_dir" log -1 --format='%h %s' > "$vendor_dir/dvbi-client-js/VERSION" 2>/dev/null || true

cp "$work/dashjs/package/dist/dash.all.min.js" "$vendor_dir/dashjs/"
cp "$work/dashjs/package/dist/dash.all.min.js.LICENSE.txt" "$vendor_dir/dashjs/"
cp "$work/dashjs/package/LICENSE.md" "$vendor_dir/dashjs/LICENSE.md" 2>/dev/null || \
   cp "$work/dashjs/package/LICENSE" "$vendor_dir/dashjs/LICENSE"
echo "$DASHJS_VERSION" > "$vendor_dir/dashjs/VERSION"

cp "$work/hlsjs/package/dist/hls.min.js" "$vendor_dir/hlsjs/"
cp "$work/hlsjs/package/LICENSE" "$vendor_dir/hlsjs/LICENSE"
echo "$HLSJS_VERSION" > "$vendor_dir/hlsjs/VERSION"

echo "Player files are in $vendor_dir"
