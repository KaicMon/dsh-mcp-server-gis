#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
harness_root="$project_root/build_dev/vendor/deepseek-harness"
renderer_root="$project_root/integrations/deepseek-harness/renderer"
workspace_link="$harness_root/packages/client/gis-mcp-renderer"
local_node="$project_root/build_dev/toolchains/node-v22.21.1/bin"

if [[ -x "$local_node/node" ]]; then
    export PATH="$local_node:$PATH"
fi

command -v node >/dev/null || {
    echo 'A native WSL Node.js 22.19+ or 24+ installation is required.' >&2
    exit 2
}
command -v pnpm >/dev/null || {
    corepack prepare pnpm@11.7.0 --activate
    corepack enable
}
test -f "$harness_root/pnpm-workspace.yaml" || {
    echo "Pinned Harness source is missing: $harness_root" >&2
    exit 2
}

# The symlink keeps all authored code in this repository-owned integration
# directory while letting the pinned Harness build apply its exact client
# closure-bundle format and platform externals.
if [[ -e "$workspace_link" && ! -L "$workspace_link" ]]; then
    echo "Refusing to replace non-symlink workspace path: $workspace_link" >&2
    exit 2
fi
ln -sfn "$renderer_root" "$workspace_link"

# The local renderer is an extra workspace member absent from the upstream
# lockfile, so this disposable build checkout intentionally refreshes only its
# build_dev-local lock state.
pnpm --dir "$harness_root" install --no-frozen-lockfile \
    --filter @local/gis-mcp-harness-renderer... \
    --network-concurrency=4 --child-concurrency=2 --reporter=append-only
pnpm --dir "$harness_root" --filter @local/gis-mcp-harness-renderer bundle
test -f "$renderer_root/lib/client.js"
echo "Renderer bundle ready: $renderer_root/lib/client.js"
