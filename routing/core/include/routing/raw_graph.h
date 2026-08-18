#pragma once

#include "routing/osm_parser.h"

#include <cstdint>
#include <vector>

namespace routing {

using OsmNodeId = std::int64_t;
using OsmWayId = std::int64_t;

struct Coordinate {
    double longitude = 0.0;
    double latitude = 0.0;
};

struct RawGraphNode {
    OsmNodeId osm_id = 0;
    Coordinate coordinate;
};

// A directed edge between two topology nodes. Intermediate OSM nodes stay in
// geometry as shape points and therefore do not inflate the search graph.
struct RawGraphEdge {
    OsmNodeId source = 0;
    OsmNodeId target = 0;
    OsmWayId osm_way_id = 0;
    double distance_m = 0.0;
    double duration_s = 0.0;
    RoadAttributes attributes;
    std::vector<Coordinate> geometry;
};

struct RawGraph {
    std::vector<RawGraphNode> nodes;
    std::vector<RawGraphEdge> edges;
};

}  // namespace routing
