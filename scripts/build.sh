#!/usr/bin/env bash
# Usage: scripts/build.sh pc|switch [extra cmake args]
set -euo pipefail

target="${1:-pc}"
shift || true
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${DEVKITPRO:=/opt/devkitpro}"

case "$target" in
pc)
    [ -d /ucrt64/bin ] && export PATH="/ucrt64/bin:$PATH"
    cmake -S "$root" -B "$root/build/pc" -G Ninja -DCMAKE_BUILD_TYPE=Debug "$@"
    cmake --build "$root/build/pc"
    ctest --test-dir "$root/build/pc" --output-on-failure
    ;;
switch)
    export DEVKITPRO
    "$DEVKITPRO/portlibs/switch/bin/aarch64-none-elf-cmake" -S "$root" -B "$root/build/switch" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release "$@"
    cmake --build "$root/build/switch"
    ;;
*)
    echo "unknown target: $target" >&2
    exit 1
    ;;
esac
