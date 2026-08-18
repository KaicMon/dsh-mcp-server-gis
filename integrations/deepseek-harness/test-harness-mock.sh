#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
integration_root="$project_root/integrations/deepseek-harness"
build_root="$project_root/build_dev/gis-full-debug"
local_node="$project_root/build_dev/toolchains/node-v22.21.1/bin"
[[ -x "$local_node/node" ]] && export PATH="$local_node:$PATH"

export DSH_HOME="$project_root/build_dev/harness-mock-home"
export DSH_GIS_API_KEY=mock-key
export DSH_GIS_MODEL=gis-mock
export DSH_PERMISSION_MODE=danger-full-access
export AMAP_API_KEY="${AMAP_API_KEY:-}"
export MCP_PROJECT_ROOT="$project_root"
export MCP_SERVER_BINARY="$build_root/mcp_server/mcp_server"
export MCP_PLUGIN_DIR="$build_root/mcp_server/plugins"
export MCP_LOG_DIR
MCP_LOG_DIR=$(mktemp -d "$build_root/harness-logs.XXXXXX")
export MCP_PLUGIN_STAGING_DIR
MCP_PLUGIN_STAGING_DIR=$(mktemp -d "$build_root/harness-plugin-staging.XXXXXX")
export MCP_TOOL_PROFILES_FILE="$project_root/config/tool-profiles.json"
export MCP_ROUTING_NETWORK="$project_root/routing/data/generated/nanjing-v1.route"
export MCP_ROUTING_NETWORK_VERSION=nanjing-v1
export GIS_CACHE_BACKEND=memory
export GIS_MAX_INLINE_BYTES=4096
export GIS_PUBLISH_INLINE_RESULTS=1
export GIS_DATASET_ROOT="$project_root/mcp_server/plugins/gis-analysis/test/data"
export GIS_REDIS_HOST=127.0.0.1
export GIS_REDIS_PORT=6379
export GIS_HARNESS_RENDERER="$integration_root/renderer"
export GIS_RENDERER_PLUGIN="$integration_root/renderer/lib/index.js"

# Reserve fresh loopback ports for this run. Fixed ports can accidentally reuse
# a stale Mock/Result server and turn a failed test setup into a false pass.
mock_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
result_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
export DSH_GIS_LLM_BASE_URL="http://127.0.0.1:$mock_port/v1"
export GIS_RESULT_SERVICE_URL="http://127.0.0.1:$result_port"

mkdir -p "$DSH_HOME" "$MCP_LOG_DIR" "$MCP_PLUGIN_STAGING_DIR"
python3 "$integration_root/mock_openai_server.py" "$mock_port" &
mock_pid=$!
"$build_root/gis/results/gis_result_server" "$result_port" 127.0.0.1 "$GIS_RESULT_SERVICE_URL" &
result_pid=$!
cleanup() {
    kill "$mock_pid" "$result_pid" 2>/dev/null || true
    wait "$mock_pid" "$result_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

for _ in {1..40}; do
    curl -fsS "$DSH_GIS_LLM_BASE_URL/models" >/dev/null 2>&1 && break
    sleep .1
done
# Readiness alone is insufficient if another process owns the endpoint. Both
# children must still be the processes started by this test.
kill -0 "$mock_pid" "$result_pid"
curl -fsS "$DSH_GIS_LLM_BASE_URL/models" >/dev/null

harness_pin=$(sed -n 's/^npm=//p' "$integration_root/VERSION")
run_harness() {
    local prompt=$1
    if [[ -f "$project_root/build_dev/vendor/deepseek-harness/apps/cli/src/bin.ts" ]]; then
        pnpm --dir "$project_root/build_dev/vendor/deepseek-harness" dsh \
            --profile headless --patch "$integration_root/gis-mcp.cordis.yml" "$prompt"
    else
        npx --yes "$harness_pin" --profile headless \
            --patch "$integration_root/gis-mcp.cordis.yml" "$prompt"
    fi
}

basic=$(run_harness 'BASIC_SCENARIO: validate a GeoJSON point.')
chain=$(run_harness 'CHAIN_SCENARIO: validate then buffer the point.')
error=$(run_harness 'ERROR_SCENARIO: correct an invalid CRS and retry.')
large=$(run_harness 'LARGE_SCENARIO: produce an externalized large buffer.')
dataset=$(run_harness 'DATASET_SCENARIO: inspect then spatially query places.geojson.')
printf '%s\n%s\n%s\n%s\n%s\n' "$basic" "$chain" "$error" "$large" "$dataset"
grep -q 'MOCK_GIS_BASIC_COMPLETE' <<<"$basic"
grep -q 'MOCK_GIS_CHAIN_COMPLETE' <<<"$chain"
grep -q 'MOCK_GIS_ERROR_COMPLETE' <<<"$error"
grep -q 'MOCK_GIS_LARGE_COMPLETE' <<<"$large"
grep -q 'MOCK_GIS_DATASET_COMPLETE' <<<"$dataset"
grep -q 'geometry_validate' "$MCP_LOG_DIR"/*.log
grep -q 'geometry_buffer' "$MCP_LOG_DIR"/*.log
grep -q 'UNSUPPORTED_CRS' "$MCP_LOG_DIR"/*.log
grep -q 'resultId' "$MCP_LOG_DIR"/*.log
grep -q 'Found 2 feature(s)' "$MCP_LOG_DIR"/*.log
echo 'Harness deterministic basic/chain/error/large/dataset GIS loops passed'
