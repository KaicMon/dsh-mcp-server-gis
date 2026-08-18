#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/../.." && pwd)
integration_root="$project_root/integrations/deepseek-harness"
build_root="$project_root/build_dev/gis-full-debug"
harness_source="$project_root/build_dev/vendor/deepseek-harness"
"$integration_root/apply-harness-patches.sh"
local_node="$project_root/build_dev/toolchains/node-v22.21.1/bin"
[[ -x "$local_node/node" ]] && export PATH="$local_node:$PATH"

if [[ -z "${DSH_GIS_API_KEY:-}" ]]; then
    echo 'DSH_GIS_API_KEY is required for real Qwen acceptance.' >&2
    exit 2
fi
if [[ -z "${AMAP_API_KEY:-}" ]]; then
    echo 'AMAP_API_KEY is required for online LBS acceptance scenarios.' >&2
    exit 2
fi

export DSH_GIS_LLM_BASE_URL="${DSH_GIS_LLM_BASE_URL:-https://dashscope.aliyuncs.com/compatible-mode/v1}"
export DSH_GIS_MODEL="${DSH_GIS_MODEL:-qwen-plus}"
export DSH_GIS_SESSION_COMPRESSION=none
export DSH_PERMISSION_MODE=danger-full-access
export DSH_HOME="${DSH_HOME:-$project_root/build_dev/harness-qwen-home}"
export MCP_PROJECT_ROOT="$project_root"
export MCP_SERVER_BINARY="${MCP_SERVER_BINARY:-$build_root/mcp_server/mcp_server}"
export MCP_PLUGIN_DIR="${MCP_PLUGIN_DIR:-$build_root/mcp_server/plugins}"
export MCP_LOG_DIR="${MCP_LOG_DIR:-$build_root/qwen-acceptance-logs}"
export MCP_PLUGIN_STAGING_DIR="${MCP_PLUGIN_STAGING_DIR:-$build_root/qwen-acceptance-staging}"
export MCP_TOOL_PROFILES_FILE="$project_root/config/tool-profiles.json"
export MCP_TOOL_PROFILE=gis-agent
export MCP_ROUTING_NETWORK="${MCP_ROUTING_NETWORK:-$project_root/routing/data/generated/nanjing-v1.route}"
export MCP_ROUTING_NETWORK_VERSION="${MCP_ROUTING_NETWORK_VERSION:-nanjing-v1}"
export GIS_RESULT_SERVICE_PORT="${GIS_RESULT_SERVICE_PORT:-18094}"
export GIS_RESULT_SERVICE_URL="${GIS_RESULT_SERVICE_URL:-http://127.0.0.1:$GIS_RESULT_SERVICE_PORT}"
export GIS_DATASET_ROOT="${GIS_DATASET_ROOT:-$project_root/mcp_server/plugins/gis-analysis/test/data}"
export GIS_CACHE_BACKEND="${GIS_CACHE_BACKEND:-memory}"
export GIS_REDIS_HOST="${GIS_REDIS_HOST:-127.0.0.1}"
export GIS_REDIS_PORT="${GIS_REDIS_PORT:-6379}"
export GIS_MAX_INLINE_BYTES="${GIS_MAX_INLINE_BYTES:-32768}"

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
report_dir="$project_root/build_dev/qwen-acceptance/$timestamp"
mkdir -p "$report_dir" "$DSH_HOME" "$MCP_LOG_DIR" "$MCP_PLUGIN_STAGING_DIR"

result_server="$build_root/gis/results/gis_result_server"
"$result_server" "$GIS_RESULT_SERVICE_PORT" 127.0.0.1 "$GIS_RESULT_SERVICE_URL" &
result_pid=$!
cleanup() {
    kill "$result_pid" 2>/dev/null || true
    wait "$result_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

run_harness() {
    local prompt=$1
    if [[ -f "$harness_source/apps/cli/src/bin.ts" ]]; then
        pnpm --dir "$harness_source" dsh --profile headless \
            --patch "$integration_root/gis-mcp.cordis.yml" "$prompt"
    else
        local harness_pin
        harness_pin=$(sed -n 's/^npm=//p' "$integration_root/VERSION")
        npx --yes "$harness_pin" --profile headless \
            --patch "$integration_root/gis-mcp.cordis.yml" "$prompt"
    fi
}

scenario_ids=(
    nearby_transit_route
    local_gis_analysis
    route_comparison
    provider_auto_fallback
    ambiguous_place
    large_geometry
    dataset_query
    open_ended_nearby
    open_ended_day_trip
)
scenario_prompts=(
    '帮我查找南京师范大学仙林校区附近三公里内的地铁站，并规划到最近地铁站的步行路线；说明每一步调用了什么工具。'
    '先校验点 {"type":"Point","coordinates":[118.7969,32.0603]}，再生成半径 1000 米缓冲区，并总结结果。'
    '比较南京新街口到南京博物院的步行路线与驾车路线，报告距离和预计时间，并说明推荐依据。'
    '使用 provider_route 的 auto 模式规划南京市内一条驾车路线，说明最终 Provider、是否发生 fallback 以及依据。'
    '查询“鼓楼”的地理编码。如果存在多个候选，不要静默选择，明确说明歧义并列出主要候选。'
    '围绕南京市中心生成 20 公里、quadrantSegments=32 的缓冲区；不要把完整大 Geometry 复述到答案，只给摘要和地图链接。'
    '查看 places.geojson 数据集信息，再查询 118.79~118.81、32.05~32.07 矩形范围内的要素并总结。'
    '查询南京新街口附近 3 公里内的景点。'
    '我在南京师范大学，要去南京博物院周边一日游，帮我规划一下。'
)

# Open-ended requests are useful only if the agent turns intent into real GIS
# evidence. Other scenarios retain a one-call floor as a general regression
# guard; the day-trip scenario requires geocoding/search/routing as a chain.
scenario_min_mcp_calls=(1 1 1 1 1 1 1 1 3)
scenario_required_tools=("" "" "" "" "" "" "" "search_nearby_by_place" "")

# A comma-separated subset makes targeted reruns cheap after fixing one Tool or
# prompt. The default remains the complete seven-scenario acceptance suite.
scenario_filter="${QWEN_ACCEPTANCE_SCENARIOS:-all}"
scenario_filter=${scenario_filter// /}
should_run_scenario() {
    local id=$1
    [[ "$scenario_filter" == "all" || ",${scenario_filter}," == *",${id},"* ]]
}

report="$report_dir/report.md"
{
    echo '# Qwen GIS/LBS acceptance report'
    echo
    echo "- UTC run: $timestamp"
    echo "- Model: $DSH_GIS_MODEL"
    echo "- Base URL: $DSH_GIS_LLM_BASE_URL"
    echo "- Scenario filter: $scenario_filter"
    echo '- Credentials: supplied through environment; values not recorded'
    echo
    echo '| Scenario | Exit | Duration ms | Tokens | MCP calls | Automatic gate | Manual verdict |'
    echo '|---|---:|---:|---:|---:|---|---|'
} >"$report"

passed_processes=0
attempted_processes=0
for index in "${!scenario_ids[@]}"; do
    id=${scenario_ids[$index]}
    should_run_scenario "$id" || continue
    attempted_processes=$((attempted_processes + 1))
    prompt=${scenario_prompts[$index]}
    output_file="$report_dir/$id.txt"
    # The marker associates the fresh one-shot Harness session and MCP logs
    # with this scenario without relying on generated session identifiers.
    marker="$report_dir/.$id.start"
    touch "$marker"
    start_ms=$(date +%s%3N)
    set +e
    run_harness "$prompt" >"$output_file" 2>&1
    status=$?
    set -e
    end_ms=$(date +%s%3N)
    duration_ms=$((end_ms - start_ms))
    session_file=$(find "$DSH_HOME/sessions-gis-jsonl" -type f \
        \( -name 'session.jsonl' -o -name 'session.jsonl.zstd' \) \
        -newer "$marker" -printf '%T@ %p\n' 2>/dev/null \
        | sort -nr | head -n 1 | cut -d' ' -f2-)
    token_total=unavailable
    mcp_tool_calls=unavailable
    automatic_gate=fail
    usage_file=
    if [[ -n "$session_file" ]]; then
        session_copy="$report_dir/$id.session${session_file##*session}"
        cp "$session_file" "$session_copy"
        usage_file="$report_dir/$id.usage.json"
        if pnpm --dir "$harness_source" exec tsx \
            "$integration_root/summarize-session.mts" \
            "$harness_source" "$session_file" >"$usage_file"; then
            token_total=$(sed -n 's/.*"totalTokens":\([0-9][0-9]*\).*/\1/p' "$usage_file")
            token_total=${token_total:-unavailable}
            mcp_tool_calls=$(sed -n 's/.*"mcpToolCalls":\([0-9][0-9]*\).*/\1/p' "$usage_file")
            mcp_tool_calls=${mcp_tool_calls:-unavailable}
        fi
    fi

    minimum_calls=${scenario_min_mcp_calls[$index]}
    required_tool=${scenario_required_tools[$index]}
    required_tool_present=true
    if [[ -n "$required_tool" ]] &&
       ! grep -q "mcp__gis__${required_tool}" "$usage_file" 2>/dev/null; then
        required_tool_present=false
    fi
    if [[ $status -eq 0 && "$mcp_tool_calls" != unavailable \
          && $mcp_tool_calls -ge $minimum_calls && "$required_tool_present" == true ]]; then
        automatic_gate=pass
    else
        # A successful CLI exit with no GIS evidence must not count as a pass.
        status=1
    fi
    [[ $status -ne 0 ]] || passed_processes=$((passed_processes + 1))

    scenario_log_dir="$report_dir/$id.mcp-logs"
    mkdir -p "$scenario_log_dir"
    find "$MCP_LOG_DIR" -maxdepth 1 -type f -newer "$marker" \
        -exec cp -t "$scenario_log_dir" {} + 2>/dev/null || true
    rm -f "$marker"
    printf '| `%s` | %d | %d | %s | %s (min %d) | %s | pending |\n' \
        "$id" "$status" "$duration_ms" "$token_total" "$mcp_tool_calls" \
        "$minimum_calls" "$automatic_gate" >>"$report"
done

if [[ $attempted_processes -eq 0 ]]; then
    echo "No scenario matched QWEN_ACCEPTANCE_SCENARIOS=$scenario_filter" >&2
    exit 3
fi

{
    echo
    echo "Process success: $passed_processes/$attempted_processes"
    echo
    echo 'A human must review each output for task correctness before replacing'
    echo '`pending` with `pass` or `fail`; exit code alone is not model-quality evidence.'
    echo
    echo 'Deterministic hot-update evidence is produced separately by:'
    echo '`bash integrations/deepseek-harness/test-harness-hot-reload.sh`.'
} >>"$report"

echo "Qwen acceptance artifacts: $report_dir"
if [[ $passed_processes -ne $attempted_processes ]]; then
    echo "Automatic acceptance failed: $passed_processes/$attempted_processes scenarios passed." >&2
    exit 1
fi
