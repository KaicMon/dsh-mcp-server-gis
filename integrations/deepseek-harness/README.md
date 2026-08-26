# DeepSeek Harness GIS integration

This directory is the repository-owned integration layer for official
DeepSeek Harness. Upstream remains unmodified. `VERSION` pins both the exact
source commit used by deterministic headless tests and the nearest published
Web bundle. The Cordis patches attach this C++ server through the official
`@deepseek-ai/dsh-mcp-client` plugin.

It also ships `@local/dsh-mcp-control-plane`, a Cordis Host plugin that
observes the `mcp__gis__*` Tool namespace and reports synchronized Tool-set
generations. It does not open a second MCP connection or grant arbitrary
process-control privileges. Build it once with
`bash integrations/deepseek-harness/build-control-plane.sh`.

`@local/dsh-mcp-rag-bridge` is also installed as a read-only catalog observer.
Build it with `bash integrations/deepseek-harness/build-rag-bridge.sh`. It
measures the complete MCP schema catalog and can preview candidates, but the
configured `selectionEnabled: false` deliberately keeps all current Tool
schemas visible to Qwen until the Agent Loop filtering phase is implemented.

## Prerequisites

- Node.js 22.19+ or 24+;
- the Debug GIS build under `build_dev/gis-full-debug`;
- a generated `.route` network for local routing;
- an Amap key for online geocoding, POI, and walking routes;
- a DashScope key for real Qwen acceptance.

## Start the Web UI

```bash
cmake -S . -B build_dev/gis-full-debug \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_GIS=ON -DBUILD_ROUTING=ON
cmake --build build_dev/gis-full-debug -j2
bash integrations/deepseek-harness/build-control-plane.sh
bash integrations/deepseek-harness/build-rag-bridge.sh
export AMAP_API_KEY='...'
export DSH_GIS_API_KEY='...'
export DSH_GIS_LLM_BASE_URL='https://dashscope.aliyuncs.com/compatible-mode/v1'
export DSH_GIS_MODEL='qwen-plus'
export MCP_ROUTING_NETWORK="$PWD/routing/data/generated/nanjing-v1.route"
integrations/deepseek-harness/run-web.sh
```

Open `http://127.0.0.1:3080`. The integration patch configures DashScope as an
OpenAI-compatible provider. Confirm the concrete Qwen model through provider
discovery before formal acceptance.

The model-facing names are server-qualified, for example
`mcp__gis__geocode`. The C++ `gis-agent` profile filters both discovery and
execution. Large GeoJSON returns `resultId + mapUrl`; opening `mapUrl` uses the
standalone Leaflet result page.

## Deterministic acceptance

```bash
bash integrations/deepseek-harness/test-harness-mock.sh
bash integrations/deepseek-harness/test-harness-hot-reload.sh
```

These cover basic calls, chained calls, correction after a Tool error, large
result externalization, and in-session atomic plugin replacement with Harness
tool-list re-sync.

For a credential-free Web smoke test, run:

```bash
bash integrations/deepseek-harness/run-web-mock.sh
```

Then submit `CHAIN_SCENARIO: validate then buffer the point.`. The conversation
should show the repository-local GIS card and its Leaflet map. The Web command
uses published `@deepseek-ai/dsh@0.1.0-rc.6`; the pinned rc.5 source checkout is
retained for deterministic headless tests because its full optional Web build
requires unrelated E2B and subagent development dependencies.

## Real Qwen acceptance

Export both credentials in the current shell, then run the acceptance suite:

```bash
export DSH_GIS_API_KEY='...'
export AMAP_API_KEY='...'
bash integrations/deepseek-harness/run-qwen-acceptance.sh
```

The script runs nine representative GIS/LBS conversations and writes raw
outputs, readable Harness session traces, per-scenario MCP logs, token summaries,
and `report.md` under `build_dev/qwen-acceptance/<UTC timestamp>/`. Credential
values are never copied into these artifacts. A successful process exit only
proves that the conversation completed; review each raw answer and replace the
report's `pending` verdict with `pass` or `fail` for model-quality acceptance.
Set `DSH_GIS_MODEL` if DashScope discovery requires a model other than the
default `qwen-plus`.

After a focused fix, rerun only affected scenarios with a comma-separated
filter, for example:

```bash
QWEN_ACCEPTANCE_SCENARIOS='nearby_transit_route,route_comparison,dataset_query' \
  bash integrations/deepseek-harness/run-qwen-acceptance.sh
```

The final two scenarios are intentionally short natural-language requests: a
nearby-attraction query and an open-ended day trip. The nearby query must use
`search_nearby_by_place`; a lone `geocode` call is an automatic failure. The
day-trip scenario must still produce a multi-step evidence chain.
The repository-local Harness plugin injects GIS decision rules that require
real-place answers to use GIS Tools. Acceptance also counts durable
`tool/call` events: ordinary scenarios require at least one GIS MCP call and
the day-trip scenario requires at least three, so a fluent incomplete answer is
reported as an automatic failure even when the CLI exits successfully.
