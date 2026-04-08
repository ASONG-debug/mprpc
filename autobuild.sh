#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_TYPE="Debug"
JOBS=""
CLEAN=0

usage() {
    cat <<'EOF'
Usage: ./autobuild.sh [options]

Options:
  --debug           Build with Debug configuration (default)
  --release         Build with Release configuration
  --clean           Remove the build directory before building
  -j, --jobs <N>    Number of parallel build jobs
  -h, --help        Show this help message
EOF
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: required command not found: $1" >&2
        exit 1
    fi
}

default_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v getconf >/dev/null 2>&1; then
        getconf _NPROCESSORS_ONLN
    else
        echo 4
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        -j|--jobs)
            if [[ $# -lt 2 ]]; then
                echo "error: $1 requires a value" >&2
                usage
                exit 1
            fi
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

require_cmd cmake

if command -v make >/dev/null 2>&1; then
    BUILD_TOOL="make"
else
    echo "error: make not found" >&2
    exit 1
fi

if [[ -z "${JOBS}" ]]; then
    JOBS="$(default_jobs)"
fi

if [[ "${CLEAN}" -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

echo "==> Project root : ${ROOT_DIR}"
echo "==> Build dir    : ${BUILD_DIR}"
echo "==> Build type   : ${BUILD_TYPE}"
echo "==> Jobs         : ${JOBS}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" -- -j"${JOBS}"

echo "==> Build completed"
echo "==> Library output    : ${ROOT_DIR}/lib"
echo "==> Executable output : ${ROOT_DIR}/bin"
