#!/usr/bin/env bash
# Verify the installed runtime layout without credentials, network access, or
# an external city routing file. It proves that dlopen discovery works from the
# same prefix that will be archived in a GitHub Release.
set -euo pipefail

prefix=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) prefix=${2:?missing value for --prefix}; shift 2 ;;
        --help)
            echo 'Usage: packaging/verify-install.sh --prefix <install-prefix>'
            exit 0
            ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

test -n "$prefix" || { echo '--prefix is required' >&2; exit 2; }
server="$prefix/bin/mcp_server"
plugins="$prefix/lib/mcp-server-gis/plugins"
profiles="$prefix/share/mcp-server-gis/config/tool-profiles.json"

test -x "$server" || { echo "Missing server: $server" >&2; exit 1; }
test -d "$plugins" || { echo "Missing plugin directory: $plugins" >&2; exit 1; }
test -f "$profiles" || { echo "Missing Tool Profile: $profiles" >&2; exit 1; }

output=$(printf '%s\n%s\n' \
  '{"jsonrpc":"2.0","id":"package-init","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"package-verify","version":"1.0"}}}' \
  '{"jsonrpc":"2.0","id":"package-list","method":"tools/list"}' \
  | timeout 15s "$server" \
      --plugins "$plugins" \
      --logs "$prefix/var/log/mcp-server-gis" \
      --tool-profile gis-agent \
      --tool-profiles-file "$profiles")

grep -F '"id":"package-init"' <<<"$output" >/dev/null
grep -F '"id":"package-list"' <<<"$output" >/dev/null
grep -F '"name":"geometry_validate"' <<<"$output" >/dev/null
grep -F '"name":"geocode"' <<<"$output" >/dev/null

echo "Installed GIS MCP Runtime verified: $prefix"
