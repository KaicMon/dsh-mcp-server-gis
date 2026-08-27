#!/usr/bin/env bash
# Install a locally built GIS MCP Runtime without root privileges.
#
# This script deliberately installs only the C++ runtime. The DeepSeek Harness
# UI integration remains a separate DSH plugin package so Node dependencies
# and platform-specific native dependencies do not get mixed in one archive.
set -euo pipefail

project_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$project_root/build_dev/gis-full-debug"
# Use the conventional per-user executable prefix. Callers that need an
# application-specific location can set MCP_INSTALL_PREFIX or pass --prefix.
prefix="${MCP_INSTALL_PREFIX:-$HOME/.local}"

usage() {
    cat <<'EOF'
Usage: packaging/install-local.sh [options]

Install a build produced by this checkout with CMake's install rules.

Options:
  --build-dir <path>  CMake build directory (default: build_dev/gis-full-debug)
  --prefix <path>     Install prefix (default: ~/.local)
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) build_dir=${2:?missing value for --build-dir}; shift 2 ;;
        --prefix) prefix=${2:?missing value for --prefix}; shift 2 ;;
        --help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

test -f "$build_dir/CMakeCache.txt" || {
    echo "CMake build directory not configured: $build_dir" >&2
    echo "Run: cmake -S . -B build_dev/gis-full-debug -DBUILD_GIS=ON -DBUILD_ROUTING=ON" >&2
    exit 2
}

cmake --build "$build_dir" --target mcp_server gis-analysis map-services osm-routing
cmake --install "$build_dir" --component Runtime --prefix "$prefix"

echo "Installed MCP Runtime: $prefix/bin/mcp_server"
echo "Installed plugins:     $prefix/lib/mcp-server-gis/plugins"
echo "DSH patch template:    $prefix/share/mcp-server-gis/dsh/gis-mcp.cordis.patch.example.yml"
echo
echo "Copy the template into your DSH profile and replace __MCP_PREFIX__."
