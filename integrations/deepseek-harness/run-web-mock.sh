#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
integration_root="$project_root/integrations/deepseek-harness"
export DSH_GIS_API_KEY=mock-key
export DSH_GIS_MODEL=gis-mock
export DSH_PERMISSION_MODE=danger-full-access
export GIS_PUBLISH_INLINE_RESULTS=1
export GIS_MAX_INLINE_BYTES=4096
export GIS_DATASET_ROOT="$project_root/mcp_server/plugins/gis-analysis/test/data"
# The pinned rc.5 source runs headless tests, but a full Web source build pulls
# optional E2B/Claude documentation and test dependencies. The published rc.6
# bundle is the nearest compatible prebuilt Web surface for visual acceptance.
export HARNESS_USE_NPM=1

mock_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
export DSH_GIS_LLM_BASE_URL="http://127.0.0.1:$mock_port/v1"
python3 "$integration_root/mock_openai_server.py" "$mock_port" &
mock_pid=$!
cleanup() {
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
"$integration_root/run-web.sh"
