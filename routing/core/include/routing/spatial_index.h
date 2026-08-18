#pragma once

#include "routing/csr_graph.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace routing {

struct RoadCandidate {
    EdgeId edge = 0;
    Coordinate projected;
    double distance_m = 0.0;
    double segment_fraction = 0.0;
    std::uint32_t geometry_index = 0;
    double distance_from_edge_start_m = 0.0;
};

class SpatialIndex {
public:
    // Indexes every geometry segment into a uniform lon/lat grid. The grid is
    // deliberately independent of MCP/HTTP and is immutable after construction.
    explicit SpatialIndex(const CsrGraph& graph, double cell_size_degrees = 0.01);

    // Returns projections sorted by perpendicular distance. A directed road is
    // represented by its directed EdgeId, which is needed for exact snapping.
    [[nodiscard]] std::vector<RoadCandidate> Nearest(
        Coordinate query, double radius_m, std::size_t maximum_candidates = 8) const;

private:
    struct SegmentRef {
        // geometry_index is an absolute index into CsrGraph::geometry and
        // denotes the first endpoint of this small line segment.
        EdgeId edge;
        std::uint32_t geometry_index;
    };

    [[nodiscard]] std::int64_t CellKey(std::int32_t x, std::int32_t y) const noexcept;
    [[nodiscard]] std::int32_t Cell(double coordinate) const noexcept;

    const CsrGraph& graph_;
    double cell_size_degrees_;
    std::unordered_map<std::int64_t, std::vector<SegmentRef>> cells_;
};

}  // namespace routing
