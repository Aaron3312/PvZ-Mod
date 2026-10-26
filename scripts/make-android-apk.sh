#!/bin/bash
# Builds the arm64-v8a APK without vcpkg, the same way CI's prebuilt route does: SDL2 (shared, for
# SDLActivity), libpng, libjpeg-turbo and libopenmpt (with its bundled minimp3, stb_vorbis and miniz,
# so it links standalone) are built with the NDK into WORK_DIR, then libmain.so, then Gradle packages
# them with -PusePrebuiltLibs=true. Signed with the debug key unless PVZP_KEYSTORE_FILE is set.
#
# Usage: scripts/make-android-apk.sh WORK_DIR [OUTPUT_APK]
# Needs: ANDROID_NDK (r27+), ANDROID_HOME (platform 36), a JDK 17+, cmake, ninja, curl.
# JOBS (default 2) limits parallel compiles. Downloads and dependency builds in WORK_DIR are reused.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mkdir -p "$1" && cd "$1" && pwd)"
OUT_APK="${2:-$ROOT/dist/pvz-portable-android-arm64-v8a.apk}"
JOBS="${JOBS:-2}"
: "${ANDROID_NDK:?set ANDROID_NDK to the NDK folder}"
: "${ANDROID_HOME:?set ANDROID_HOME to the SDK folder}"

SDL_VER=2.32.10
PNG_VER=1.6.59
JPEG_VER=3.1.4
OPENMPT_VER=0.8.9
GRADLE_VER=8.12

PREFIX="$WORK/prefix"
SDL_PREFIX="$WORK/sdl2-android"
HOST_TAG="$(ls "$ANDROID_NDK/toolchains/llvm/prebuilt" | head -1)"
STRIP="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/llvm-strip"
NDK_ARGS=(
	-G Ninja
	-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake"
	-DANDROID_ABI=arm64-v8a
	-DANDROID_PLATFORM=android-24
	-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
	-DCMAKE_BUILD_TYPE=Release
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

fetch() {	# url, file name in WORK/dl
	[ -s "$WORK/dl/$2" ] || curl -fsSL -o "$WORK/dl/$2" "$1"
}
mkdir -p "$WORK/dl" "$WORK/src" "$WORK/obj"
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz" "SDL2-$SDL_VER.tar.gz"
fetch "https://github.com/pnggroup/libpng/archive/refs/tags/v$PNG_VER.tar.gz" "libpng-$PNG_VER.tar.gz"
fetch "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$JPEG_VER/libjpeg-turbo-$JPEG_VER.tar.gz" "libjpeg-turbo-$JPEG_VER.tar.gz"
fetch "https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-$OPENMPT_VER+release.makefile.tar.gz" "libopenmpt-$OPENMPT_VER.tar.gz"
fetch "https://services.gradle.org/distributions/gradle-$GRADLE_VER-bin.zip" "gradle-$GRADLE_VER-bin.zip"
for f in "SDL2-$SDL_VER" "libpng-$PNG_VER" "libjpeg-turbo-$JPEG_VER" "libopenmpt-$OPENMPT_VER"; do
	[ -d "$WORK/src/$f" ] || [ -d "$WORK/src/$f+release" ] || tar xzf "$WORK/dl/$f.tar.gz" -C "$WORK/src"
done
[ -d "$WORK/gradle-$GRADLE_VER" ] || unzip -q "$WORK/dl/gradle-$GRADLE_VER-bin.zip" -d "$WORK"
OPENMPT_SRC="$WORK/src/libopenmpt-$OPENMPT_VER+release"

if [ ! -f "$PREFIX/lib/libpng16.a" ]; then
	cmake -S "$WORK/src/libpng-$PNG_VER" -B "$WORK/obj/png" "${NDK_ARGS[@]}" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF
	cmake --build "$WORK/obj/png" -j"$JOBS" && cmake --install "$WORK/obj/png"
fi
if [ ! -f "$PREFIX/lib/libjpeg.a" ]; then
	cmake -S "$WORK/src/libjpeg-turbo-$JPEG_VER" -B "$WORK/obj/jpeg" "${NDK_ARGS[@]}" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DCMAKE_INSTALL_LIBDIR=lib
	cmake --build "$WORK/obj/jpeg" -j"$JOBS" && cmake --install "$WORK/obj/jpeg"
fi
if [ ! -f "$PREFIX/lib/libopenmpt.a" ]; then
	# The file list comes from libopenmpt's own ndk-build makefile.
	awk '/^LOCAL_SRC_FILES \+= \\/{f=1;next} f{ if ($0 ~ /include \$\(BUILD/) exit; gsub(/[\\\t ]/,""); if (length($0)) print }' \
		"$OPENMPT_SRC/build/android_ndk/Android.mk" > "$WORK/obj/openmpt-sources.txt"
	cmake -S "$ROOT/android/openmpt" -B "$WORK/obj/openmpt" "${NDK_ARGS[@]}" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DOPENMPT_SRC="$OPENMPT_SRC" -DOPENMPT_SOURCES="$WORK/obj/openmpt-sources.txt"
	cmake --build "$WORK/obj/openmpt" -j"$JOBS" && cmake --install "$WORK/obj/openmpt"
fi
if [ ! -f "$SDL_PREFIX/lib/libSDL2.so" ]; then
	cmake -S "$WORK/src/SDL2-$SDL_VER" -B "$WORK/obj/sdl2" "${NDK_ARGS[@]}" -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
		-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
	cmake --build "$WORK/obj/sdl2" -j"$JOBS" && cmake --install "$WORK/obj/sdl2"
fi

cmake -S "$ROOT" -B "$WORK/obj/game" "${NDK_ARGS[@]}" \
	-DCMAKE_FIND_ROOT_PATH="$PREFIX;$SDL_PREFIX" -DSDL2_DIR="$SDL_PREFIX/lib/cmake/SDL2"
cmake --build "$WORK/obj/game" -j"$JOBS"

JNILIBS="$ROOT/android/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$JNILIBS"
cp -L "$WORK/obj/game/libmain.so" "$SDL_PREFIX/lib/libSDL2.so" "$JNILIBS/"
for so in "$JNILIBS"/*.so; do "$STRIP" --strip-unneeded "$so"; done

# Gradle compiles SDL's Java classes from ../sdl2-src; they must match the native libSDL2.so.
[ -e "$ROOT/sdl2-src" ] || ln -s "$WORK/src/SDL2-$SDL_VER" "$ROOT/sdl2-src"
echo "sdk.dir=$ANDROID_HOME" > "$ROOT/android/local.properties"
(cd "$ROOT/android" && "$WORK/gradle-$GRADLE_VER/bin/gradle" assembleRelease -PusePrebuiltLibs=true \
	--no-daemon --max-workers="$JOBS" -Dorg.gradle.jvmargs=-Xmx2g)

mkdir -p "$(dirname "$OUT_APK")"
cp "$ROOT/android/app/build/outputs/apk/release/app-release.apk" "$OUT_APK"
echo "APK: $OUT_APK"
