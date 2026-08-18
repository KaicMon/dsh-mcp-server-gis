#!/usr/bin/env bash
set -euo pipefail

server_binary="$1"
plugin_test="$2"
port="${3:-18090}"
mode="${4:-external}"
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

if [[ "$mode" == "inline-published" ]]; then
    # Reproduce Harness/Web behavior: compact GeoJSON remains inline for the
    # next Agent call, while a second copy is published for the map card.
    GIS_RESULT_SERVICE_URL="$base_url" GIS_MAX_INLINE_BYTES=32768 \
        GIS_PUBLISH_INLINE_RESULTS=1 "$plugin_test"
else
    GIS_RESULT_SERVICE_URL="$base_url" GIS_MAX_INLINE_BYTES=1 \
        EXPECT_EXTERNAL_RESULT=1 "$plugin_test"
fi
