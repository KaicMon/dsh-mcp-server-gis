#pragma once

#include "routing/raw_graph.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace routing {

using NodeId = std::uint32_t;
using EdgeId = std::uint32_t;

// Compact adjacency representation used by every online query.
//
// Outgoing edges of node u occupy [offsets[u], offsets[u + 1]). Every edge
// attribute uses the same EdgeId index. Geometry has a second CSR-like offset
// array because one graph edge can contain multiple OSM shape points.
struct CsrGraph {
    std::vector<OsmNodeId> node_osm_ids;
    std::vector<Coordinate> node_coordinates;
    std::vector<std::uint64_t> offsets;
    std::vector<NodeId> targets;
    std::vector<float> distance_m;
    std::vector<float> duration_s;
    std::vector<OsmWayId> edge_osm_way_ids;
    std::vector<std::uint64_t> geometry_offsets;
    std::vector<Coordinate> geometry;

    [[nodiscard]] std::size_t NodeCount() const noexcept { return node_osm_ids.size(); }
    [[nodiscard]] std::size_t EdgeCount() const noexcept { return targets.size(); }
    void Validate() const;
};

class CsrBuilder {
public:
    // Assigns stable dense IDs according to RawGraph::nodes order and sorts
    // directed edges by source before constructing the CSR arrays.
    [[nodiscard]] CsrGraph Build(const RawGraph& raw) const;
};

class NetworkFile {
public:
    // Persists a validated immutable snapshot. The file contains a format
    // version and payload checksum; it is not the OSM source format.
    static void Save(const CsrGraph& graph, const std::filesystem::path& output);
    // Rejects unknown versions, truncated vectors, invalid offsets, and a
    // checksum mismatch before returning a queryable graph.
    [[nodiscard]] static CsrGraph Load(const std::filesystem::path& input);
};

}  // namespace routing
