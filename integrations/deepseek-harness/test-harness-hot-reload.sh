#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
integration_root="$project_root/integrations/deepseek-harness"
build_root="$project_root/build_dev/gis-full-debug"
local_node="$project_root/build_dev/toolchains/node-v22.21.1/bin"
[[ -x "$local_node/node" ]] && export PATH="$local_node:$PATH"

export DSH_HOME="$project_root/build_dev/harness-hot-reload-home"
export DSH_GIS_API_KEY=mock-key
export DSH_GIS_MODEL=gis-mock
export DSH_PERMISSION_MODE=danger-full-access
export MCP_PROJECT_ROOT="$project_root"
export MCP_SERVER_BINARY="$build_root/mcp_server/mcp_server"
export MCP_TOOL_PROFILES_FILE="$project_root/config/tool-profiles.json"
export MCP_TOOL_PROFILE=harness-hot-reload-test
export MCP_PLUGIN_DIR
MCP_PLUGIN_DIR=$(mktemp -d "$build_root/hot-plugin-source.XXXXXX")
export MCP_PLUGIN_STAGING_DIR
MCP_PLUGIN_STAGING_DIR=$(mktemp -d "$build_root/hot-plugin-staging.XXXXXX")
export MCP_LOG_DIR
MCP_LOG_DIR=$(mktemp -d "$build_root/hot-plugin-logs.XXXXXX")
export AMAP_API_KEY=''
export MCP_ROUTING_NETWORK=''
export MCP_ROUTING_NETWORK_VERSION=''
export GIS_RESULT_SERVICE_URL=''
export GIS_CACHE_BACKEND=memory
export GIS_REDIS_HOST=127.0.0.1
export GIS_REDIS_PORT=6379
export GIS_RENDERER_PLUGIN="$integration_root/renderer/lib/index.js"

# Use an isolated endpoint and verify that the child we started owns it. This
# prevents a stale Mock server from satisfying the hot-update test.
mock_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
export DSH_GIS_LLM_BASE_URL="http://127.0.0.1:$mock_port/v1"

runtime_plugin="$build_root/mcp_server/test/plugins/runtime-v1/libruntime_plugin_v1.so"
replacement_plugin="$build_root/mcp_server/test/plugins/valid/libloader_valid_plugin.so"
cp "$runtime_plugin" "$MCP_PLUGIN_DIR/runtime-plugin.so"
export GIS_HOT_RELOAD_MARKER="$MCP_PLUGIN_DIR/.first-call-complete"

python3 "$integration_root/mock_openai_server.py" "$mock_port" &
mock_pid=$!
mutation_pid=''
cleanup() {
    [[ -z "$mutation_pid" ]] || kill "$mutation_pid" 2>/dev/null || true
    kill "$mock_pid" 2>/dev/null || true
    wait "$mock_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

for _ in {1..40}; do
    curl -fsS "$DSH_GIS_LLM_BASE_URL/models" >/dev/null 2>&1 && break
    sleep .1
done
kill -0 "$mock_pid"
curl -fsS "$DSH_GIS_LLM_BASE_URL/models" >/dev/null

# Replace one public Tool with another while the same Harness Agent session is
# alive. The server stages the new .so, atomically publishes a registry
# snapshot, and emits tools/list_changed; Harness must re-sync before call 2.
(
    for _ in {1..100}; do
        [[ -e "$GIS_HOT_RELOAD_MARKER" ]] && break
        sleep .1
    done
    [[ -e "$GIS_HOT_RELOAD_MARKER" ]]
    rm -f "$MCP_PLUGIN_DIR/runtime-plugin.so"
    cp "$replacement_plugin" "$MCP_PLUGIN_DIR/loader-valid-plugin.so"
) &
mutation_pid=$!

output=$(pnpm --dir "$project_root/build_dev/vendor/deepseek-harness" dsh \
    --profile headless --patch "$integration_root/gis-mcp.cordis.yml" \
    'HOT_RELOAD_SCENARIO: call the original tool, then the replacement tool.')
wait "$mutation_pid"
printf '%s\n' "$output"
grep -q 'MOCK_GIS_HOT_RELOAD_COMPLETE' <<<"$output"
grep -q 'runtime_tool' "$MCP_LOG_DIR"/*.log
grep -q 'loader_test_tool' "$MCP_LOG_DIR"/*.log
echo 'Harness plugin hot-update and tool-list refresh passed'
