#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

mkdir -p Release
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

python3 tools/embed_patch_resources.py \
    patch-version.txt \
    data/res \
    data/original \
    "$BUILD_DIR"

x86_64-w64-mingw32-windres \
    "$BUILD_DIR/patch_resources.rc" \
    "$BUILD_DIR/patch_resources.o"

x86_64-w64-mingw32-g++ \
    -std=c++20 \
    -O2 \
    -static \
    -static-libgcc \
    -static-libstdc++ \
    BinaryReader.cpp \
    Hash.cpp \
    Main.cpp \
    MurmurHash3.cpp \
    NPKFile.cpp \
    PatchInstaller.cpp \
    "$BUILD_DIR/patch_resources.o" \
    -I"$BUILD_DIR" \
    -I"$HOME/vcpkg/installed/x64-mingw-static/include" \
    "$HOME/vcpkg/installed/x64-mingw-static/lib/liblz4.a" \
    -lcomctl32 \
    -lshell32 \
    -lole32 \
    -mwindows \
    -o Release/SMCNPK.exe