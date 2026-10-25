#!/bin/sh
# Packs build/pvz-portable into "Plants vs. Zombies.app": bundled dylibs (dylibbundler + SDL3,
# which Homebrew's sdl2-compat loads at runtime), icon, Info.plist and a launcher that passes -resdir.
#
# Usage: scripts/make-macos-app.sh [OUTPUT_DIR] [GAME_DATA_DIR]
#   OUTPUT_DIR     where the .app goes (default: dist)
#   GAME_DATA_DIR  folder with main.pak and properties/ to copy into the app. At launch the app prefers
#                  ~/Library/Application Support/io.github.wszqkzqk/PvZPortable/gamedata-mod (extracted,
#                  for mods), then its own copy, then .../PvZPortable/gamedata.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-$ROOT/dist}"
DATA_DIR="$2"
APP="$OUT_DIR/Plants vs. Zombies.app"
VERSION="$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null || echo 0)"

[ -x "$ROOT/build/pvz-portable" ] || { echo "Build first: cmake -G Ninja -B build && cmake --build build" >&2; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS/libs" "$APP/Contents/Resources"

cp "$ROOT/build/pvz-portable" "$APP/Contents/MacOS/"
strip -x "$APP/Contents/MacOS/pvz-portable"
dylibbundler -of -b -x "$APP/Contents/MacOS/pvz-portable" -d "$APP/Contents/MacOS/libs" -p @executable_path/libs/ >/dev/null 2>&1
install -m 755 "$(brew --prefix sdl3)/lib/libSDL3.dylib" "$APP/Contents/MacOS/libs/libSDL3.dylib"

if [ -n "$DATA_DIR" ]; then
	cp "$DATA_DIR/main.pak" "$APP/Contents/Resources/"
	cp -R "$DATA_DIR/properties" "$APP/Contents/Resources/"
fi

cat > "$APP/Contents/MacOS/PvZ" <<'EOF'
#!/bin/sh
# Game data: the extracted, moddable gamedata-mod folder if it exists (scripts/extract-pak.py),
# then the data inside the app, then the shared gamedata folder.
DIR="$(cd "$(dirname "$0")" && pwd)"
DATA="$HOME/Library/Application Support/io.github.wszqkzqk/PvZPortable"
if [ -d "$DATA/gamedata-mod/properties" ]; then
	RES="$DATA/gamedata-mod"
elif [ -f "$DIR/../Resources/main.pak" ]; then
	RES="$DIR/../Resources"
else
	RES="$DATA/gamedata"
fi
exec "$DIR/pvz-portable" -resdir="$RES" "$@"
EOF
chmod 755 "$APP/Contents/MacOS/PvZ"

ICONSET="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
	sips -z $s $s "$ROOT/icon.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
	sips -z $((s * 2)) $((s * 2)) "$ROOT/icon.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>Plants vs. Zombies</string>
	<key>CFBundleDisplayName</key><string>Plants vs. Zombies</string>
	<key>CFBundleIdentifier</key><string>com.aaron3312.pvz-portable</string>
	<key>CFBundleExecutable</key><string>PvZ</string>
	<key>CFBundleIconFile</key><string>AppIcon</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>$VERSION</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
	<key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

codesign --force --deep -s - "$APP" >/dev/null 2>&1
codesign --verify --deep "$APP"
echo "$APP"
