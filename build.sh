#!/usr/bin/env bash
# Builds bin/Headroom.exe with MSYS2's CLANG64 toolchain (clang++, libc++, lld, llvm-windres).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin

# Manifest (common controls 6, per-monitor DPI)
(cd src && llvm-windres -O coff -i app.rc -o ../bin/app.res.o)

clang++ -std=c++23 -Os -s -static -mwindows -municode \
  -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE \
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  src/standalone.cpp bin/app.res.o -o bin/Headroom.exe \
  -lole32 -lshell32 -lgdi32 -luser32 -ldwmapi -ladvapi32 -lmsimg32 -lgdiplus -ld2d1 -ldwrite \
  -lshlwapi -lcomctl32 -lcomdlg32 -luxtheme -lshcore -luuid -loleaut32 -lversion -lmmdevapi

ls -l bin/Headroom.exe
sha256sum bin/Headroom.exe
