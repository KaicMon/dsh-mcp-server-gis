#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
harness_root="$project_root/build_dev/vendor/deepseek-harness"
plugin_root="$project_root/integrations/deepseek-harness/rag-bridge"
workspace_link="$harness_root/packages/extensions/mcp-rag-bridge"
local_node="$project_root/build_dev/toolchains/node-v22.21.1/bin"

[[ -x "$local_node/node" ]] && export PATH="$local_node:$PATH"
command -v node >/dev/null || { echo 'Native WSL Node.js 22.19+ or 24+ is required.' >&2; exit 2; }
command -v pnpm >/dev/null || { corepack prepare pnpm@11.7.0 --activate; corepack enable; }
test -f "$harness_root/pnpm-workspace.yaml" || { echo "Pinned Harness source is missing: $harness_root" >&2; exit 2; }

if [[ -e "$workspace_link" && ! -L "$workspace_link" ]]; then
    echo "Refusing to replace non-symlink workspace path: $workspace_link" >&2
    exit 2
fi
ln -sfn "$plugin_root" "$workspace_link"
if [[ -L "$plugin_root/node_modules" ]]; then unlink "$plugin_root/node_modules"; fi
pnpm --dir "$harness_root" install --no-frozen-lockfile \
    --filter @local/dsh-mcp-rag-bridge... \
    --network-concurrency=4 --child-concurrency=2 --reporter=append-only
pnpm --dir "$harness_root" --filter @local/dsh-mcp-rag-bridge bundle
test -f "$plugin_root/lib/index.mjs"
node "$plugin_root/test/plugin-contract.mjs"
echo "RAG bridge bundle ready: $plugin_root/lib/index.mjs"
