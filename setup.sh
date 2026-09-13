#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
command -v git >/dev/null || { echo "Git is required." >&2; exit 1; }
command -v cmake >/dev/null || { echo "CMake is required." >&2; exit 1; }

if [[ ! -d "${root_dir}/vcpkg" ]]; then
  git clone https://github.com/microsoft/vcpkg.git "${root_dir}/vcpkg"
  "${root_dir}/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
fi

cmake -S "${root_dir}" -B "${root_dir}/build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${root_dir}/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DACEPS_BUILD_GUI=ON

echo "AcePS configured. Build with: cmake --build build"
