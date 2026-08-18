#!/usr/bin/env bash
set -euo pipefail

server_binary="$1"
plugin_test="$2"
port="${3:-18089}"
base_url="http://127.0.0.1:${port}"

timeout 20s "$server_binary" "$port" 127.0.0.1 "$base_url" >/dev/null 2>&1 &
server_pid=$!
cleanup() {
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT
for _ in 1 2 3 4 5; do
    curl --fail --silent "$base_url/api/v1/status" >/dev/null && break
    sleep 1
done

# A one-byte limit deterministically forces the fixture route across the HTTP
# process boundary. The plugin test then asserts resultId replaces inline data.
GIS_RESULT_SERVICE_URL="$base_url" GIS_MAX_INLINE_BYTES=1 \
    EXPECT_EXTERNAL_RESULT=1 "$plugin_test"
