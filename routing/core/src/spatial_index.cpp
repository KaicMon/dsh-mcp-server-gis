#include "routing/spatial_index.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace routing {
namespace {

struct Projection {
    Coordinate coordinate;
    double distance_m;
    double fraction;
};

double SegmentLength(Coordinate lhs, Coordinate rhs) {
    constexpr double kMetersPerDegree = 111'320.0;
    const double latitude = (lhs.latitude + rhs.latitude) * 0.5 *
                            3.14159265358979323846 / 180.0;
    const double dx = (rhs.longitude - lhs.longitude) * kMetersPerDegree * std::cos(latitude);
    const double dy = (rhs.latitude - lhs.latitude) * kMetersPerDegree;
    return std::hypot(dx, dy);
}

Projection Project(Coordinate query, Coordinate start, Coordinate end) {
    // Use a local equirectangular plane around the query. At road-segment scale
    // this is fast and sufficiently accurate; routing distances still use the
    // graph's spherical length calculation.
    constexpr double kMetersPerDegree = 111'320.0;
    const double latitude_radians = query.latitude * 3.14159265358979323846 / 180.0;
    const double x_scale = kMetersPerDegree * std::max(0.01, std::cos(latitude_radians));
    const double ax = (start.longitude - query.longitude) * x_scale;
    const double ay = (start.latitude - query.latitude) * kMetersPerDegree;
    const double bx = (end.longitude - query.longitude) * x_scale;
    const double by = (end.latitude - query.latitude) * kMetersPerDegree;
    const double dx = bx - ax;
    const double dy = by - ay;
    const double denominator = dx * dx + dy * dy;
    const double fraction = denominator == 0.0
                                ? 0.0
                                : std::clamp(-(ax * dx + ay * dy) / denominator, 0.0, 1.0);
    const double px = ax + fraction * dx;
    const double py = ay + fraction * dy;
    return {{start.longitude + fraction * (end.longitude - start.longitude),
             start.latitude + fraction * (end.latitude - start.latitude)},
            std::hypot(px, py), fraction};
}

std::uint64_t SegmentKey(EdgeId edge, std::uint32_t geometry_index) {
    return (static_cast<std::uint64_t>(edge) << 32U) | geometry_index;
}

}  // namespace

SpatialIndex::SpatialIndex(const CsrGraph& graph, double cell_size_degrees)
    : graph_(graph), cell_size_degrees_(cell_size_degrees) {
    if (cell_size_degrees_ <= 0.0) throw std::invalid_argument("Spatial cell size must be positive");
    // Insert a segment into every grid cell touched by its bounding box. Long
    // segments therefore remain discoverable even when the query is far from
    // the cell containing the segment's first coordinate.
    for (EdgeId edge = 0; edge < graph_.EdgeCount(); ++edge) {
        const auto begin = graph_.geometry_offsets[edge];
        const auto end = graph_.geometry_offsets[edge + 1];
        for (std::uint64_t index = begin; index + 1 < end; ++index) {
            const auto& lhs = graph_.geometry[index];
            const auto& rhs = graph_.geometry[index + 1];
            const auto min_x = Cell(std::min(lhs.longitude, rhs.longitude));
            const auto max_x = Cell(std::max(lhs.longitude, rhs.longitude));
            const auto min_y = Cell(std::min(lhs.latitude, rhs.latitude));
            const auto max_y = Cell(std::max(lhs.latitude, rhs.latitude));
            for (auto x = min_x; x <= max_x; ++x) {
                for (auto y = min_y; y <= max_y; ++y) {
                    cells_[CellKey(x, y)].push_back(
                        {edge, static_cast<std::uint32_t>(index)});
                }
            }
        }
    }
}

std::int32_t SpatialIndex::Cell(double coordinate) const noexcept {
    return static_cast<std::int32_t>(std::floor(coordinate / cell_size_degrees_));
}

std::int64_t SpatialIndex::CellKey(std::int32_t x, std::int32_t y) const noexcept {
    const auto packed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
                        static_cast<std::uint32_t>(y);
    return static_cast<std::int64_t>(packed);
}

std::vector<RoadCandidate> SpatialIndex::Nearest(
    Coordinate query, double radius_m, std::size_t maximum_candidates) const {
    if (radius_m < 0.0 || maximum_candidates == 0) return {};
    constexpr double kMetersPerDegree = 111'320.0;
    const double lat_delta = radius_m / kMetersPerDegree;
    const double lon_delta = lat_delta /
        std::max(0.01, std::cos(query.latitude * 3.14159265358979323846 / 180.0));
    // One segment can occur in several queried cells; de-duplicate it before
    // projection so distance and candidate counts remain deterministic.
    std::unordered_set<std::uint64_t> visited;
    std::vector<RoadCandidate> candidates;
    for (auto x = Cell(query.longitude - lon_delta); x <= Cell(query.longitude + lon_delta); ++x) {
        for (auto y = Cell(query.latitude - lat_delta); y <= Cell(query.latitude + lat_delta); ++y) {
            const auto bucket = cells_.find(CellKey(x, y));
            if (bucket == cells_.end()) continue;
            for (const auto& segment : bucket->second) {
                if (!visited.insert(SegmentKey(segment.edge, segment.geometry_index)).second) continue;
                const auto projection = Project(query,
                    graph_.geometry[segment.geometry_index],
                    graph_.geometry[segment.geometry_index + 1]);
                if (projection.distance_m <= radius_m) {
                    // Store distance along the complete directed edge. The
                    // RoutingEngine uses it to charge only the traversed prefix
                    // or suffix when a coordinate lies inside an edge.
                    const auto edge_begin = graph_.geometry_offsets[segment.edge];
                    double along_edge_m = 0.0;
                    for (std::uint64_t point = edge_begin; point < segment.geometry_index; ++point) {
                        along_edge_m += SegmentLength(
                            graph_.geometry[point], graph_.geometry[point + 1]);
                    }
                    const double segment_length = SegmentLength(
                        graph_.geometry[segment.geometry_index],
                        graph_.geometry[segment.geometry_index + 1]);
                    along_edge_m += projection.fraction * segment_length;
                    candidates.push_back({segment.edge, projection.coordinate,
                                          projection.distance_m, projection.fraction,
                                          segment.geometry_index, along_edge_m});
                }
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.distance_m < rhs.distance_m;
    });
    if (candidates.size() > maximum_candidates) candidates.resize(maximum_candidates);
    return candidates;
}

}  // namespace routing
