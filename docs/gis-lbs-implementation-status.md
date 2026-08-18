# GIS/LBS implementation status

Last updated: 2026-08-16.

This file records implementation evidence for
`gis-lbs-mcp-platform-plan.md` and `deepseek-harness-gis-integration-plan.md`.

## Implemented

- Three dynamically loaded GIS plugins are available:
  - `gis-analysis`: geometry validation/transform/predicate/metric/buffer,
    `dataset_info`, and `features_within`.
  - `map-services`: geocode, reverse geocode, POI text/nearby search,
    compound place-based nearby search, administrative search, provider
    routing, and provider status.
  - `osm-routing`: route planning, nearest-road lookup, map matching, and
    routing status.
- GDAL datasets are read through a RAII facade. Dataset MCP calls accept only
  paths under `GIS_DATASET_ROOT`; absolute paths and canonical paths escaping
  that root are rejected.
- Amap and Local OSM implement project-owned `MapProvider` DTOs. Amap GCJ-02
  coordinates are normalized to WGS84 before entering MCP responses.
- Memory/Redis response cache, finite retry, token-bucket rate limiting,
  normalized provider errors, and provider/cache metadata are implemented.
- Every GIS/LBS Tool emits the versioned GIS envelope in both
  `structuredContent` and a bounded durable text projection for Harness.
- Memory result storage, inline-size/point thresholds, HTTP publishing, result
  API, and Leaflet map URLs are implemented and cross-process tested.
- Harness Web uses GIS Result HTTP port `18096`, separate from routing HTTP
  port `8088`; this avoids WSL Windows-localhost forwarding selecting the
  wildcard routing listener for a GIS result URL.
- DeepSeek Harness source is pinned to commit
  `47f943859bef60e4160492346772ded9b24f765a`. Its official MCP client has passed
  a deterministic Mock LLM -> Agent -> MCP -> C++ GIS Tool -> Agent loop.
- A repository-local Harness renderer registers all GIS Profile tools and
  renders inline GeoJSON or external result URLs as GIS cards.
- The published Harness rc.6 Web surface starts at `127.0.0.1:3080`, its boot
  manifest contains `@local/gis-mcp-harness-renderer`, and the served client
  bundle contains the complete current GIS Tool registry. The exact rc.5
  source commit remains the deterministic headless-test target.
- H3 deterministic Harness coverage is complete: basic, chained-call,
  correction-after-error, externalized large-result, GDAL dataset chaining,
  and an in-session atomic plugin replacement followed by
  `tools/list_changed` re-sync all pass.
- Deterministic Harness tests allocate isolated loopback ports and verify their
  own Mock/Result child processes, preventing stale services from producing a
  false pass.
- Tool Profile `gis-agent` limits the set injected into Harness. Tool RAG stays
  deferred until tool-count and retrieval-evaluation data justify it.
- The Harness-facing Profile exposes `provider_route` for both walking and
  driving. The local-OSM-only `route_plan` remains available outside this
  Profile, avoiding a model selecting a driving-only Tool for a walking task.
- High-frequency deterministic LBS chains are exposed as compound Tools rather
  than depending on an LLM to finish every atomic step.
  `search_nearby_by_place` performs geocoding and nearby POI search inside one
  plugin call, reports `providerOperations=2`, and refuses to silently select
  among multiple geocode candidates. Atomic `geocode` and `nearby_search`
  remain available for exploratory Agent workflows.
- Real-model acceptance now records MCP Tool names and enforces semantic gates:
  the short nearby-query scenario must call `search_nearby_by_place`, while a
  process that only calls `geocode` is rejected even if it exits successfully.

## Deferred deployment work

- H5 browser verification is complete. The Harness GIS card rendered the Tool
  result, and the external Leaflet result page was opened successfully from
  Windows through `127.0.0.1:18096`.
- H6 protected remote Streamable HTTP deployment and RedisResultStore remain a
  later deployment phase, as explicitly scoped by the integration plan.

## Verification

```bash
cmake --build build_dev/gis-full-debug -j4
ctest --test-dir build_dev/gis-full-debug --output-on-failure
bash integrations/deepseek-harness/build-renderer.sh
build_dev/toolchains/node-v22.21.1/bin/node \
  integrations/deepseek-harness/renderer/test/envelope.test.mjs
bash integrations/deepseek-harness/test-harness-mock.sh
bash integrations/deepseek-harness/test-harness-hot-reload.sh
# Requires DSH_GIS_API_KEY and AMAP_API_KEY:
bash integrations/deepseek-harness/run-qwen-acceptance.sh
```

Latest C++ result: **30/30 tests passed**. The renderer build and envelope
contract test also pass. All six deterministic Harness Agent scenarios pass.

## Real Qwen acceptance

H4 is complete. DashScope `qwen-plus` and Amap were exercised on 2026-08-16.
The initial run passed five of seven semantic reviews; after fixing the
driving-only Tool selection conflict and published-inline dataset response,
the focused rerun passed all three affected scenarios. The combined latest
result is **7/7 (100%)**, above the initial 80% target. Raw answers, token
summaries, readable Session traces, and MCP logs are retained under:

- `build_dev/qwen-acceptance/20260816T080545Z/`
- `build_dev/qwen-acceptance/20260816T081726Z/`

The current C++ suite contains **30 tests**; the added test covers the exact
Harness policy where compact GeoJSON remains inline while a map copy is also
published.
