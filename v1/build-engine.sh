#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
APP_NAME='DILIYAN STEM MASTER ENGINE'
ROOT="$(pwd)"
BUILD="$(mktemp -d "$ROOT/engine-build.XXXXXX")"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE" "$BUILD"' EXIT
DIST="$ROOT/engine-dist"
mkdir -p "$DIST"
APP="$BUILD/$APP_NAME.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
CXX=(xcrun clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -arch arm64 -mmacosx-version-min=13.0)
"${CXX[@]}" v1/Tests.cpp -framework AudioToolbox -framework CoreFoundation -o "$BUILD/DSPTests"
"$BUILD/DSPTests" "$BUILD/dsp-fixtures" | tee "$DIST/DSP-TESTS.txt"
"${CXX[@]}" v1/Engine.cpp -framework AudioToolbox -framework CoreFoundation -o "$APP/Contents/MacOS/DiliyanEngine"
xcrun swiftc v1/EngineApp.swift -O -swift-version 5 -target arm64-apple-macosx13.0 \
  -sdk "$(xcrun --sdk macosx --show-sdk-path)" -framework AppKit -framework AVFoundation \
  -framework UniformTypeIdentifiers -o "$APP/Contents/MacOS/DiliyanStemMaster"
/usr/bin/python3 - "$APP/Contents/Info.plist" <<'PY'
import plistlib, sys
with open(sys.argv[1], 'wb') as f:
    plistlib.dump({
        'CFBundleExecutable': 'DiliyanStemMaster',
        'CFBundleIdentifier': 'local.diliyan.stemmaster.engine',
        'CFBundleName': 'DILIYAN STEM MASTER ENGINE',
        'CFBundleDisplayName': 'DILIYAN STEM MASTER ENGINE',
        'CFBundlePackageType': 'APPL',
        'CFBundleShortVersionString': '0.1.0',
        'CFBundleVersion': '1',
        'LSMinimumSystemVersion': '13.0',
        'NSHighResolutionCapable': True,
        'NSPrincipalClass': 'NSApplication',
    }, f)
PY
cp v1/ENGINE-V1-DE.md "$APP/Contents/Resources/ENGINE-V1-DE.md"
plutil -lint "$APP/Contents/Info.plist"
for exe in DiliyanEngine DiliyanStemMaster; do
  test "$(lipo -archs "$APP/Contents/MacOS/$exe")" = arm64
  codesign --force --sign - "$APP/Contents/MacOS/$exe"
done
codesign --force --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
/usr/bin/python3 v1/integration.py "$APP/Contents/MacOS/DiliyanEngine" "$BUILD/integration" | tee "$DIST/INTEGRATION-TESTS.txt"
/usr/bin/python3 - "$APP/Contents/MacOS/DiliyanStemMaster" "$BUILD/integration/VOCAL.wav" <<'PY'
import subprocess, sys
subprocess.run([sys.argv[1], '--smoke-test', sys.argv[2]], check=True, timeout=30)
PY
ditto -c -k --sequesterRsrc --keepParent "$APP" "$DIST/DILIYAN-ENGINE-V1-arm64.zip"
ditto "$APP" "$STAGE/$APP_NAME.app"
ln -s /Applications "$STAGE/Programme"
cp v1/ENGINE-V1-DE.md "$STAGE/Bitte zuerst lesen.md"
hdiutil create -volname 'DILIYAN ENGINE V1' -srcfolder "$STAGE" -ov -format UDZO "$DIST/DILIYAN-ENGINE-V1-arm64.dmg"
hdiutil verify "$DIST/DILIYAN-ENGINE-V1-arm64.dmg"
{
  echo 'Experimental V1, ad hoc signed. NOT Developer ID signed. NOT notarized.'
  echo 'Native DSP tests, synthetic end-to-end render and UI smoke test passed.'
  echo 'Downloaded-file Gatekeeper behavior, actual M1 playback and listening quality still require user testing.'
  sw_vers
  uname -m
  xcodebuild -version
  xcrun swiftc --version
  xcrun clang++ --version
  echo "Source revision: ${GITHUB_SHA:-local}"
  codesign -dv "$APP" 2>&1
} > "$DIST/BUILD-REPORT.txt"
(cd "$DIST" && shasum -a 256 DILIYAN-ENGINE-V1-arm64.zip DILIYAN-ENGINE-V1-arm64.dmg > SHA256SUMS.txt)
