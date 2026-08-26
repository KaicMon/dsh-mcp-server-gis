#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
build_root="$project_root/build_dev/gis-full-debug"
integration_root="$project_root/integrations/deepseek-harness"
harness_pin=$(sed -n 's/^npm=//p' "$integration_root/VERSION")
harness_source="$project_root/build_dev/vendor/deepseek-harness"
# The packaged Web app already contains its compiled frontend and dependency
# graph. Source mode remains available for Harness development, but requires a
# complete pnpm workspace checkout and an explicit HARNESS_USE_NPM=0.
HARNESS_USE_NPM="${HARNESS_USE_NPM:-1}"
local_node="$project_root/build_dev/toolchains/node-v22.21.1/bin"
if [[ -x "$local_node/node" ]]; then
    export PATH="$local_node:$PATH"
fi

if ! command -v node >/dev/null; then
    echo 'Node.js 22.19+ or 24+ is required by DeepSeek Harness.' >&2
    exit 2
fi
node -e 'const [major,minor]=process.versions.node.split(".").map(Number); if (!(major>=24 || (major===22 && minor>=19))) process.exit(1)' || {
    echo "Unsupported Node.js version: $(node --version)" >&2
    exit 2
}

export MCP_PROJECT_ROOT="$project_root"
export DSH_HOME="${DSH_HOME:-$project_root/build_dev/harness-web-home}"
export MCP_SERVER_BINARY="${MCP_SERVER_BINARY:-$build_root/mcp_server/mcp_server}"
export MCP_PLUGIN_DIR="${MCP_PLUGIN_DIR:-$build_root/mcp_server/plugins}"
export MCP_LOG_DIR="${MCP_LOG_DIR:-$build_root/harness-logs}"
export MCP_PLUGIN_STAGING_DIR="${MCP_PLUGIN_STAGING_DIR:-$build_root/harness-plugin-staging}"
export MCP_TOOL_PROFILES_FILE="$project_root/config/tool-profiles.json"
export GIS_HARNESS_RENDERER="$integration_root/renderer"
export AMAP_API_KEY="${AMAP_API_KEY:-}"
export MCP_ROUTING_NETWORK="${MCP_ROUTING_NETWORK:-$project_root/routing/data/generated/nanjing-v1.route}"
export MCP_ROUTING_NETWORK_VERSION="${MCP_ROUTING_NETWORK_VERSION:-nanjing-v1}"
export GIS_RESULT_SERVICE_PORT="${GIS_RESULT_SERVICE_PORT:-18096}"
export GIS_RESULT_SERVICE_URL="${GIS_RESULT_SERVICE_URL:-http://127.0.0.1:$GIS_RESULT_SERVICE_PORT}"
export GIS_CACHE_BACKEND="${GIS_CACHE_BACKEND:-memory}"
export GIS_REDIS_HOST="${GIS_REDIS_HOST:-127.0.0.1}"
export GIS_REDIS_PORT="${GIS_REDIS_PORT:-6379}"
mkdir -p "$MCP_LOG_DIR" "$MCP_PLUGIN_STAGING_DIR"
renderer_profile_dir="$DSH_HOME/profiles/node_modules/@local"
mkdir -p "$renderer_profile_dir"
ln -sfn "$integration_root/renderer" \
    "$renderer_profile_dir/gis-mcp-harness-renderer"
ln -sfn "$integration_root/control-plane" \
    "$renderer_profile_dir/dsh-mcp-control-plane"
ln -sfn "$integration_root/rag-bridge" \
    "$renderer_profile_dir/dsh-mcp-rag-bridge"

test -x "$MCP_SERVER_BINARY" || {
    echo "MCP server not built: $MCP_SERVER_BINARY" >&2
    exit 2
}
test -f "$GIS_HARNESS_RENDERER/lib/client.js" || {
    echo 'Harness GIS renderer is not built; run integrations/deepseek-harness/build-renderer.sh' >&2
    exit 2
}
test -f "$integration_root/control-plane/lib/index.mjs" || {
    echo 'MCP control-plane plugin is not built; run integrations/deepseek-harness/build-control-plane.sh' >&2
    exit 2
}
test -f "$integration_root/rag-bridge/lib/index.mjs" || {
    echo 'MCP RAG bridge is not built; run integrations/deepseek-harness/build-rag-bridge.sh' >&2
    exit 2
}

result_server="$build_root/gis/results/gis_result_server"
test -x "$result_server" || {
    echo "GIS result server not built: $result_server" >&2
    exit 2
}
"$result_server" "$GIS_RESULT_SERVICE_PORT" 127.0.0.1 "$GIS_RESULT_SERVICE_URL" &
result_pid=$!
cleanup() {
    kill "$result_pid" 2>/dev/null || true
    wait "$result_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [[ "$HARNESS_USE_NPM" != 1 &&
      -f "$harness_source/apps/cli/src/bin.ts" ]] && command -v pnpm >/dev/null; then
    "$integration_root/apply-harness-patches.sh"
    pnpm --dir "$harness_source" dsh web \
        --patch "$integration_root/gis-mcp-web.cordis.yml"
else
    npx --yes "$harness_pin" web \
        --patch "$integration_root/gis-mcp-web.cordis.yml"
fi
