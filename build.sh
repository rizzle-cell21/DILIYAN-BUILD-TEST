#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
APP_NAME='DILIYAN STEM MASTER – Build Test'
APP="build/$APP_NAME.app"
mkdir -p "$APP/Contents/MacOS" dist
xcrun swiftc Sources/main.swift -O -target arm64-apple-macosx13.0 \
  -sdk "$(xcrun --sdk macosx --show-sdk-path)" -framework AppKit \
  -o "$APP/Contents/MacOS/DiliyanBuildTest"
/usr/bin/python3 - "$APP/Contents/Info.plist" <<'PY'
import plistlib, sys
with open(sys.argv[1], 'wb') as f:
    plistlib.dump({
        'CFBundleExecutable': 'DiliyanBuildTest',
        'CFBundleIdentifier': 'local.diliyan.stemmaster.buildtest',
        'CFBundleName': 'DILIYAN STEM MASTER – Build Test',
        'CFBundleDisplayName': 'DILIYAN STEM MASTER – Build Test',
        'CFBundlePackageType': 'APPL',
        'CFBundleShortVersionString': '0.1.0',
        'CFBundleVersion': '1',
        'LSMinimumSystemVersion': '13.0',
        'NSHighResolutionCapable': True,
    }, f)
PY
plutil -lint "$APP/Contents/Info.plist"
test "$(lipo -archs "$APP/Contents/MacOS/DiliyanBuildTest")" = arm64
codesign --force --sign - "$APP"
codesign --verify --strict --verbose=2 "$APP"
# Native window smoke test; this does not test downloaded-file Gatekeeper behavior.
/usr/bin/python3 - "$APP/Contents/MacOS/DiliyanBuildTest" <<'PY'
import subprocess, sys
subprocess.run([sys.argv[1], '--smoke-test'], check=True, timeout=30)
PY
ditto -c -k --sequesterRsrc --keepParent "$APP" dist/DILIYAN-Build-Test-arm64.zip
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/$APP_NAME.app"
ln -s /Applications "$STAGE/Programme"
hdiutil create -volname 'DILIYAN Build Test' -srcfolder "$STAGE" \
  -ov -format UDZO dist/DILIYAN-Build-Test-arm64.dmg
hdiutil verify dist/DILIYAN-Build-Test-arm64.dmg
{
  echo 'Private test: ad-hoc signed; NOT Developer ID signed; NOT notarized.'
  echo 'Native window smoke test passed. Download/Gatekeeper/M1 user test still required.'
  sw_vers
  uname -m
  xcodebuild -version
  xcrun swiftc --version
  echo "Source revision: ${GITHUB_SHA:-local}"
  codesign -dv "$APP" 2>&1
} > dist/BUILD-REPORT.txt
(cd dist && shasum -a 256 DILIYAN-Build-Test-arm64.zip DILIYAN-Build-Test-arm64.dmg > SHA256SUMS.txt)
