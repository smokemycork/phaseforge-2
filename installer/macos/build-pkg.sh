#!/bin/bash
# PhaseForge - macOS installer (.pkg) builder.
#
#   ./build-pkg.sh <cmake-build-dir> <version> [output-dir]
#
# Installs:
#   PhaseForge.vst3 -> /Library/Audio/Plug-Ins/VST3
#   PhaseForge.component (Audio Unit) -> /Library/Audio/Plug-Ins/Components
#   PhaseForge.app (standalone) -> /Applications
#
# Optional signing (needs a paid Apple Developer account). Without these variables the
# package is ad-hoc signed and users have to right-click > Open the first time.
#   APP_SIGN_ID        "Developer ID Application: Your Name (TEAMID)"
#   INSTALLER_SIGN_ID  "Developer ID Installer: Your Name (TEAMID)"
set -euo pipefail

BUILD_DIR="${1:?usage: build-pkg.sh <cmake-build-dir> <version> [output-dir]}"
VERSION="${2:?usage: build-pkg.sh <cmake-build-dir> <version> [output-dir]}"
OUT_DIR="${3:-dist}"
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

find_artefact () { find "$BUILD_DIR" -maxdepth 5 -name "$1" -print -quit; }

VST3="$(find_artefact PhaseForge.vst3)"
AU="$(find_artefact PhaseForge.component)"
APP="$(find_artefact PhaseForge.app)"
for item in "$VST3" "$AU" "$APP"; do
  [ -n "$item" ] && [ -d "$item" ] || { echo "Missing build artefact - did the build finish?"; exit 1; }
done
echo "VST3 : $VST3"
echo "AU   : $AU"
echo "App  : $APP"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

sign_bundle () {
  if [ -n "${APP_SIGN_ID:-}" ]; then
    codesign --force --deep --options runtime --timestamp --sign "$APP_SIGN_ID" "$1"
  else
    codesign --force --deep --sign - "$1"        # ad-hoc: required for arm64 binaries to load at all
  fi
}

stage () {  # stage <bundle> <install-location> <component-id> <pkg-name>
  local bundle="$1" location="$2" id="$3" name="$4"
  local root="$WORK/root-$name"
  mkdir -p "$root"
  cp -R "$bundle" "$root/"
  sign_bundle "$root/$(basename "$bundle")"
  pkgbuild --root "$root" --install-location "$location" --identifier "$id" --version "$VERSION" "$WORK/$name.pkg"
}

stage "$VST3" "/Library/Audio/Plug-Ins/VST3"       com.phaseforge.vst3       vst3
stage "$AU"   "/Library/Audio/Plug-Ins/Components" com.phaseforge.component  au
stage "$APP"  "/Applications"                      com.phaseforge.standalone app

sed "s/__VERSION__/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

PKG="$OUT_DIR/PhaseForge-$VERSION-macOS.pkg"
if [ -n "${INSTALLER_SIGN_ID:-}" ]; then
  productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" --sign "$INSTALLER_SIGN_ID" "$PKG"
else
  productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$PKG"
fi
echo "Created $PKG"
