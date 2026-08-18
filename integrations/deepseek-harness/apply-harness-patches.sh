#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
harness_root="$project_root/build_dev/vendor/deepseek-harness"
patch_file="$project_root/integrations/deepseek-harness/patches/dashscope-tool-call-correlation.patch"

test -d "$harness_root/.git" || {
    echo "Pinned Harness checkout is missing: $harness_root" >&2
    exit 2
}

if git -C "$harness_root" apply --reverse --check "$patch_file" 2>/dev/null; then
    # Already applied; keep this helper idempotent for every Web/test launch.
    exit 0
fi
git -C "$harness_root" apply --check "$patch_file"
git -C "$harness_root" apply "$patch_file"
