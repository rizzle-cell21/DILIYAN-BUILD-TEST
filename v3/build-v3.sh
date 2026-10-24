#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
ROOT="$(pwd)"
BUILD="$(mktemp -d "$ROOT/v3-build.XXXXXX")"
STAGE="$(mktemp -d)"
trap 'rm -rf "$BUILD" "$STAGE"' EXIT
DIST="$ROOT/v3-dist"
mkdir -p "$DIST"
APP="$BUILD/DILIYAN STEM MASTER ENGINE V3.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
CXX=(xcrun clang++ -std=c++17 -O3 -pthread -Wall -Wextra -Wpedantic -Werror -arch arm64 -mmacosx-version-min=13.0)
"${CXX[@]}" v2/Tests.cpp v2/PlayerBridge.cpp -framework AudioToolbox -framework CoreFoundation -o "$BUILD/V3Tests"
"$BUILD/V3Tests" "$BUILD/unit" | tee "$DIST/DSP-TESTS.txt"
"${CXX[@]}" v3/Tests.cpp -framework AudioToolbox -framework CoreFoundation -o "$BUILD/ReferenceTests"
"$BUILD/ReferenceTests" "$BUILD/reference-unit" | tee -a "$DIST/DSP-TESTS.txt"
"${CXX[@]}" v2/Engine.cpp -framework AudioToolbox -framework CoreFoundation -o "$BUILD/UnmodifiedV2"
"${CXX[@]}" v3/Engine.cpp -framework AudioToolbox -framework CoreFoundation -o "$APP/Contents/MacOS/DiliyanEngineV3"
"${CXX[@]}" -c v2/PlayerBridge.cpp -o "$BUILD/PlayerBridge.o"
xcrun swiftc v3/EngineApp.swift "$BUILD/PlayerBridge.o" -O -swift-version 5 -target arm64-apple-macosx13.0 \
 -sdk "$(xcrun --sdk macosx --show-sdk-path)" -import-objc-header v2/PlayerBridge.h \
 -framework AppKit -framework AVFoundation -framework UniformTypeIdentifiers -framework AudioToolbox \
 -Xlinker -lc++ -o "$APP/Contents/MacOS/DiliyanStemMasterV3"
/usr/bin/python3 - "$APP/Contents/Info.plist" <<'PY'
import plistlib,sys
with open(sys.argv[1],'wb') as f:
 plistlib.dump({'CFBundleExecutable':'DiliyanStemMasterV3','CFBundleIdentifier':'local.diliyan.stemmaster.engine.v3','CFBundleName':'DILIYAN STEM MASTER ENGINE V3','CFBundleDisplayName':'DILIYAN STEM MASTER ENGINE V3','CFBundlePackageType':'APPL','CFBundleShortVersionString':'3.0.0','CFBundleVersion':'1','LSMinimumSystemVersion':'13.0','NSPrincipalClass':'NSApplication','NSHighResolutionCapable':True},f)
PY
cp v3/THIRD-PARTY-NOTICES.txt v3/README-V3-DE.md "$APP/Contents/Resources/"
plutil -lint "$APP/Contents/Info.plist"
for name in DiliyanEngineV3 DiliyanStemMasterV3; do
 test "$(lipo -archs "$APP/Contents/MacOS/$name")" = arm64
 codesign --force --sign - "$APP/Contents/MacOS/$name"
done
codesign --force --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
SOXR_PREFIX="$(brew --prefix libsoxr)"
"${CXX[@]}" v2/IndependentPeak.cpp -I"$SOXR_PREFIX/include" -L"$SOXR_PREFIX/lib" -lsoxr -framework AudioToolbox -framework CoreFoundation -o "$BUILD/IndependentPeak"
DILIYAN_SOXR_METER="$BUILD/IndependentPeak" PYTHONUNBUFFERED=1 /usr/bin/python3 v3/integration.py "$APP/Contents/MacOS/DiliyanEngineV3" "$BUILD/UnmodifiedV2" "$BUILD/integration" | tee "$DIST/INTEGRATION-TESTS.txt"
/usr/bin/python3 - "$APP/Contents/MacOS/DiliyanStemMasterV3" "$BUILD/integration/Vocal.wav" "$BUILD/integration/Reference Mix ü" <<'PY'
import subprocess,sys
subprocess.run([sys.argv[1],'--smoke-test',sys.argv[2],sys.argv[3]],check=True,timeout=60)
PY
ditto -c -k --sequesterRsrc --keepParent "$APP" "$DIST/DILIYAN-ENGINE-V3-arm64.zip"
ditto "$APP" "$STAGE/DILIYAN STEM MASTER ENGINE V3.app"
ln -s /Applications "$STAGE/Programme"
cp v3/README-V3-DE.md "$STAGE/Bitte zuerst lesen.md"
hdiutil create -volname 'DILIYAN ENGINE V3' -srcfolder "$STAGE" -ov -format UDZO "$DIST/DILIYAN-ENGINE-V3-arm64.dmg"
hdiutil verify "$DIST/DILIYAN-ENGINE-V3-arm64.dmg"
{
 echo 'Audio V3 Experimental. Ad hoc signed, NOT notarized.'
 echo 'Reference-guided mix, energy strata, review decisions; preserved V2 limiter and sample-based player.'
 echo 'Independent FFmpeg/libsoxr checks passed on synthetic tests. M1 listening test still required.'
 echo "Source revision: ${GITHUB_SHA:-local}"
 sw_vers
 uname -m
 xcodebuild -version
 xcrun swiftc --version
 codesign -dv "$APP" 2>&1
} > "$DIST/BUILD-REPORT.txt"
(cd "$DIST" && shasum -a 256 DILIYAN-ENGINE-V3-arm64.zip DILIYAN-ENGINE-V3-arm64.dmg > SHA256SUMS.txt)
