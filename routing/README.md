# Routing Engine

`routing/` contains the OSM road-network core and stays independent of MCP and HTTP.

## Runtime call chain

```text
OSM PBF
  -> TopologyBuilder       # split Ways at endpoints/intersections
  -> RawGraph              # OSM IDs and complete edge geometry
  -> CsrBuilder            # dense IDs and contiguous adjacency arrays
  -> NetworkFile           # versioned/checksummed .route snapshot

RoadNetworkStore::Stage    # load + validate + build Router/SpatialIndex
  -> Publish               # atomic shared_ptr generation switch
  -> RoutingEngine         # coordinate snapping + partial-edge route
     |- OsmRoutingPlugin   # MCP schema and JSON conversion
     `- routing_http       # HTTP and GeoJSON conversion
```

The main data invariants are:

- `offsets[u]..offsets[u+1]` contains every outgoing edge of dense node `u`;
- all edge metadata arrays share the same `EdgeId` index;
- `geometry_offsets[e]..geometry_offsets[e+1]` contains directed edge geometry;
- published graphs and spatial indexes are immutable;
- every query holds one `shared_ptr<const RoadNetwork>` generation;
- GeoJSON coordinates are always `[longitude, latitude]` in WGS84.

## Phase 1: OSM inspection

Build and test:

```bash
cmake -S . -B build_dev/routing_phase1 -DBUILD_MCP_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build_dev/routing_phase1 --target osm_parser_test osm_inspect -j
ctest --test-dir build_dev/routing_phase1 -R routing_osm_parser --output-on-failure
```

Inspect a dataset:

```bash
build_dev/routing_phase1/routing/osm_inspect routing/data/raw/nanjing-city.osm.pbf
```

The motor-vehicle filter uses an explicit road-class allowlist, excludes `area=yes`,
and respects `access`, `vehicle`, `motor_vehicle`, and `motorcar` denial tags. This is
the initial driving profile policy, not a complete OSM access-conditional evaluator.
Topology construction must preserve `oneway=-1` and the implicit forward direction of
`junction=roundabout`.
