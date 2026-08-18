#!/usr/bin/env bash
set -euo pipefail

server="$1"
profiles="$2"
gis_plugin="$3"
map_plugin="$4"
calculator_plugin="$5"
temp_dir=$(mktemp -d)
cleanup() {
    find "$temp_dir" -type f -delete 2>/dev/null || true
    find "$temp_dir" -depth -type d -empty -delete 2>/dev/null || true
}
trap cleanup EXIT
mkdir -p "$temp_dir/plugins" "$temp_dir/logs" "$temp_dir/staging"
cp "$gis_plugin" "$map_plugin" "$calculator_plugin" "$temp_dir/plugins/"

{
    printf '%s\n' '{"jsonrpc":"2.0","id":"list","method":"tools/list"}'
    printf '%s\n' '{"jsonrpc":"2.0","id":"call","method":"tools/call","params":{"name":"calculator","arguments":{"expression":"1+1"}}}'
} | "$server" --plugins "$temp_dir/plugins" --logs "$temp_dir/logs" \
        --plugin-staging "$temp_dir/staging" --tool-profile gis-agent \
        --tool-profiles-file "$profiles" >"$temp_dir/responses.jsonl"

grep '"id":"list"' "$temp_dir/responses.jsonl" | grep -q 'geometry_buffer'
if grep '"id":"list"' "$temp_dir/responses.jsonl" | grep -q 'calculator'; then
    echo 'calculator leaked into gis-agent tools/list' >&2
    exit 1
fi
grep '"id":"call"' "$temp_dir/responses.jsonl" |
    grep -q 'Tool is not available in profile: gis-agent'
