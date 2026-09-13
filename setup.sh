#!/usr/bin/env bash
# AcePS Linux setup helper: downloads vcpkg locally and installs the declared C++ dependencies.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
vcpkg_root="${project_root}/.tools/vcpkg"

for required_command in git cmake; do
    command -v "${required_command}" >/dev/null || {
        echo "Missing required command: ${required_command}. Install it, then run this script again." >&2
        exit 1
    }
done

if [[ ! -d "${vcpkg_root}/.git" ]]; then
    mkdir -p "${project_root}/.tools"
    git clone https://github.com/microsoft/vcpkg.git "${vcpkg_root}"
fi

"${vcpkg_root}/bootstrap-vcpkg.sh" -disableMetrics
"${vcpkg_root}/vcpkg" install --triplet x64-linux

echo "Setup complete. Build with: cmake -B build && cmake --build build --config Release"
