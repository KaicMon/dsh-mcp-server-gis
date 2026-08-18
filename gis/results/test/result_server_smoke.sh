#!/usr/bin/env bash
set -euo pipefail

server_binary="$1"
port="${2:-18088}"
base_url="http://127.0.0.1:${port}"
log_file="${TMPDIR:-/tmp}/gis-result-server-smoke.log"

timeout 15s "$server_binary" "$port" 127.0.0.1 "$base_url" >"$log_file" 2>&1 &
server_pid=$!
cleanup() {
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT

for _ in 1 2 3 4 5; do
    if curl --fail --silent --show-error "$base_url/api/v1/status" >/dev/null; then
        break
    fi
    sleep 1
done

response=$(curl --fail --silent --show-error -X POST \
    -H 'Content-Type: application/geo+json' \
    --data-binary '{"type":"Point","coordinates":[118.79,32.06]}' \
    "$base_url/api/v1/results?ttlSeconds=60")
result_id=$(printf '%s' "$response" |
    sed -n 's/.*"resultId":"\([^"]*\)".*/\1/p')
test -n "$result_id"

payload=$(curl --fail --silent --show-error \
    "$base_url/api/v1/results/$result_id")
printf '%s' "$payload" | grep -q '"type":"Point"'
curl --fail --silent --show-error "$base_url/results/$result_id" |
    grep -q 'GIS MCP Result'

printf 'resultId=%s\n' "$result_id"
