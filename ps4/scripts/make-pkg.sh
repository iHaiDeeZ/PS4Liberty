#!/bin/bash
# Package a staged app directory into a PS4 fake PKG (modeled on DolphinPS4's make-pkg.sh).
#
#   make-pkg.sh <stage-dir> <TITLE_ID> <title> <version XX.YY> <content-label> <out-dir>
#
# <stage-dir> must contain eboot.bin and sce_sys/icon0.png; everything else in it is packaged
# as-is. The OpenOrbis sce_module/*.prx and sce_sys/about/right.sprx are added here. Uses the
# plain homebrew SFO, which the RADV/Vulkan identity (PAID 0x3800000000000035) launches with.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ps4-env.sh"

STAGE="$(readlink -f "$1")"; TITLE_ID="$2"; TITLE="$3"; VERSION="$4"; LABEL="$5"; OUT="$(readlink -f "$6")"
TOOLS="$OPENORBIS/bin/linux"
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1

[[ "$TITLE_ID" =~ ^[A-Z]{4}[0-9]{5}$ ]] || { echo "TITLE_ID must look like ABCD12345" >&2; exit 1; }
[[ "$VERSION" =~ ^[0-9]{2}\.[0-9]{2}$ ]] || { echo "version must look like 01.00" >&2; exit 1; }
[ -f "$STAGE/eboot.bin" ] || { echo "missing $STAGE/eboot.bin" >&2; exit 1; }
[ -f "$STAGE/sce_sys/icon0.png" ] || { echo "missing $STAGE/sce_sys/icon0.png" >&2; exit 1; }

mkdir -p "$STAGE/sce_module" "$STAGE/sce_sys/about" "$OUT"
cp "$OPENORBIS/samples/piglet/sce_sys/about/right.sprx" "$STAGE/sce_sys/about/"
cp "$OPENORBIS/samples/piglet/sce_module/"*.prx "$STAGE/sce_module/"

LABEL="$(printf '%s' "$LABEL" | tr '[:lower:]' '[:upper:]' | tr -cd 'A-Z0-9')"
LABEL="$(printf '%-16s' "${LABEL:0:16}" | tr ' ' '0')"
CONTENT_ID="IV0000-${TITLE_ID}_00-${LABEL}"

SFO="$STAGE/sce_sys/param.sfo"
rm -f "$SFO"
"$TOOLS/PkgTool.Core" sfo_new "$SFO"
set_entry() { "$TOOLS/PkgTool.Core" sfo_setentry "$SFO" "$1" --type "$2" --maxsize "$3" --value "$4" >/dev/null; }
set_entry APP_TYPE Integer 4 1
set_entry ATTRIBUTE Integer 4 0
set_entry CATEGORY Utf8 4 gd
set_entry SYSTEM_VER Integer 4 0
set_entry APP_VER Utf8 8 "$VERSION"
set_entry CONTENT_ID Utf8 48 "$CONTENT_ID"
set_entry DOWNLOAD_DATA_SIZE Integer 4 0
set_entry TITLE Utf8 128 "$TITLE"
set_entry TITLE_ID Utf8 12 "$TITLE_ID"
set_entry VERSION Utf8 8 "$VERSION"

rm -f "$STAGE/pkg.gp4"
( cd "$STAGE" && "$TOOLS/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --path . ) >/dev/null
( cd "$STAGE" && "$TOOLS/PkgTool.Core" pkg_build pkg.gp4 "$OUT" ) >/dev/null
rm -f "$STAGE/pkg.gp4"
echo "$OUT/$CONTENT_ID.pkg"
