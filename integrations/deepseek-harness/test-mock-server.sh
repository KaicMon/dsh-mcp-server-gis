#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
port=18091
python3 -m py_compile "$root/mock_openai_server.py"
python3 "$root/mock_openai_server.py" "$port" &
server_pid=$!
cleanup() {
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT
sleep 1

first=$(curl --fail --silent -H 'Content-Type: application/json' \
    --data-binary '{"model":"gis-mock","messages":[{"role":"user","content":"test"}],"tools":[{"type":"function","function":{"name":"mcp__gis__geometry_validate","parameters":{"type":"object"}}}]}' \
    "http://127.0.0.1:${port}/v1/chat/completions")
printf '%s' "$first" | grep -q 'mcp__gis__geometry_validate'
second=$(curl --fail --silent -H 'Content-Type: application/json' \
    --data-binary '{"model":"gis-mock","messages":[{"role":"tool","tool_call_id":"mock-call-1","content":"ok"}]}' \
    "http://127.0.0.1:${port}/v1/chat/completions")
printf '%s' "$second" | grep -q 'MOCK_GIS_TASK_COMPLETE'
