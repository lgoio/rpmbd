#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="Release"
BUILD_DIR="build"
CLEAN=0

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  -d, --debug           Build Debug (default: Release)
  -r, --release         Build Release
  -B, --build-dir <dir> Build directory (default: build)
  -c, --clean           Remove build directory before building
  -j <n>                Parallel build jobs (default: number of CPU cores)
  -h, --help            Show this help

Examples:
  $0
  $0 --debug
  $0 --clean --release
  $0 -B out/build --debug
EOF
}

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--debug)   BUILD_TYPE="Debug"; shift ;;
    -r|--release) BUILD_TYPE="Release"; shift ;;
    -B|--build-dir)
      BUILD_DIR="${2:-}"; [[ -n "$BUILD_DIR" ]] || { echo "ERROR: missing arg for $1" >&2; exit 2; }
      shift 2
      ;;
    -c|--clean) CLEAN=1; shift ;;
    -j)
      JOBS="${2:-}"; [[ -n "$JOBS" ]] || { echo "ERROR: missing number after -j" >&2; exit 2; }
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ "$CLEAN" -eq 1 ]]; then
  rm -rf -- "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

echo "[build.sh] Configure: $BUILD_TYPE -> $BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "[build.sh] Build: -j$JOBS"
cmake --build "$BUILD_DIR" -j "$JOBS"

BIN="$BUILD_DIR/rpmbd"
if [[ -x "$BIN" ]]; then
  echo "[build.sh] Done: $BIN"
else
  echo "[build.sh] Done. Binary location depends on generator/config."
fi

