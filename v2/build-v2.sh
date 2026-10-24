#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
ROOT="$(pwd)"
BUILD="$(mktemp -d "$ROOT/v2-build.XXXXXX")"
STAGE="$(mktemp -d)"
trap 'rm -rf "$BUILD" "$STAGE"' EXIT
DIST="$ROOT/v2-dist"
mkdir -p "$DIST"
APP="$BUILD/DILIYAN STEM MASTER ENGINE V2.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
CXX=(xcrun clang++ -std=c++17 -O3 -pthread -Wall -Wextra -Wpedantic -Werror -arch arm64 -mmacosx-version-min=13.0)
"${CXX[@]}" v2/Tests.cpp v2/PlayerBridge.cpp -framework AudioToolbox -framework CoreFoundation -o "$BUILD/V2Tests"
"$BUILD/V2Tests" "$BUILD/unit" | tee "$DIST/DSP-TESTS.txt"
"${CXX[@]}" v1/Engine.cpp -framework AudioToolbox -framework CoreFoundation -o "$BUILD/UnmodifiedV1"
"${CXX[@]}" v2/Engine.cpp -framework AudioToolbox -framework CoreFoundation -o "$APP/Contents/MacOS/DiliyanEngineV2"
"${CXX[@]}" -c v2/PlayerBridge.cpp -o "$BUILD/PlayerBridge.o"
xcrun swiftc v2/EngineApp.swift "$BUILD/PlayerBridge.o" -O -swift-version 5 -target arm64-apple-macosx13.0 \
 -sdk "$(xcrun --sdk macosx --show-sdk-path)" -import-objc-header v2/PlayerBridge.h \
 -framework AppKit -framework AVFoundation -framework UniformTypeIdentifiers -framework AudioToolbox \
 -Xlinker -lc++ -o "$APP/Contents/MacOS/DiliyanStemMasterV2"
/usr/bin/python3 - "$APP/Contents/Info.plist" <<'PY'
import plistlib,sys
with open(sys.argv[1],'wb') as f:
 plistlib.dump({'CFBundleExecutable':'DiliyanStemMasterV2','CFBundleIdentifier':'local.diliyan.stemmaster.engine.v2','CFBundleName':'DILIYAN STEM MASTER ENGINE V2','CFBundleDisplayName':'DILIYAN STEM MASTER ENGINE V2','CFBundlePackageType':'APPL','CFBundleShortVersionString':'2.0.0','CFBundleVersion':'1','LSMinimumSystemVersion':'13.0','NSPrincipalClass':'NSApplication','NSHighResolutionCapable':True},f)
PY
cp v2/THIRD-PARTY-NOTICES.txt v2/README-V2-DE.md "$APP/Contents/Resources/"
plutil -lint "$APP/Contents/Info.plist"
for name in DiliyanEngineV2 DiliyanStemMasterV2; do
 test "$(lipo -archs "$APP/Contents/MacOS/$name")" = arm64
 codesign --force --sign - "$APP/Contents/MacOS/$name"
done
codesign --force --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
PYTHONUNBUFFERED=1 /usr/bin/python3 v2/integration.py "$APP/Contents/MacOS/DiliyanEngineV2" "$BUILD/UnmodifiedV1" "$BUILD/integration" | tee "$DIST/INTEGRATION-TESTS.txt"
/usr/bin/python3 - "$APP/Contents/MacOS/DiliyanStemMasterV2" "$BUILD/integration/Vocal.wav" <<'PY'
import subprocess,sys
subprocess.run([sys.argv[1],'--smoke-test',sys.argv[2]],check=True,timeout=60)
PY
ditto -c -k --sequesterRsrc --keepParent "$APP" "$DIST/DILIYAN-ENGINE-V2-arm64.zip"
ditto "$APP" "$STAGE/DILIYAN STEM MASTER ENGINE V2.app"
ln -s /Applications "$STAGE/Programme"
cp v2/README-V2-DE.md "$STAGE/Bitte zuerst lesen.md"
hdiutil create -volname 'DILIYAN ENGINE V2' -srcfolder "$STAGE" -ov -format UDZO "$DIST/DILIYAN-ENGINE-V2-arm64.dmg"
hdiutil verify "$DIST/DILIYAN-ENGINE-V2-arm64.dmg"
{
 echo 'Audio V2 Experimental. Ad hoc signed, NOT notarized.'
 echo 'Real unmasking, two phases, oversampled limiting and source-node playback.'
 echo 'Independent FFmpeg/libsoxr checks passed on synthetic tests. M1 listening test still required.'
 echo "Source revision: ${GITHUB_SHA:-local}"
 sw_vers
 uname -m
 xcodebuild -version
 xcrun swiftc --version
 codesign -dv "$APP" 2>&1
} > "$DIST/BUILD-REPORT.txt"
(cd "$DIST" && shasum -a 256 DILIYAN-ENGINE-V2-arm64.zip DILIYAN-ENGINE-V2-arm64.dmg > SHA256SUMS.txt)
