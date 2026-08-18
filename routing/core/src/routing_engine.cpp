#include "routing/routing_engine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace routing {
namespace {

NodeId EdgeSource(const CsrGraph& graph, EdgeId edge) {
    // offsets can contain repeated values for zero-outdegree nodes. upper_bound
    // correctly returns the owner of this concrete EdgeId in that case.
    return static_cast<NodeId>(
        std::upper_bound(graph.offsets.begin(), graph.offsets.end(), edge) -
        graph.offsets.begin() - 1);
}

bool SamePoint(const Coordinate& lhs, const Coordinate& rhs) {
    return std::abs(lhs.longitude - rhs.longitude) < 1e-12 &&
           std::abs(lhs.latitude - rhs.latitude) < 1e-12;
}

void Append(std::vector<Coordinate>& output, const Coordinate& point) {
    if (output.empty() || !SamePoint(output.back(), point)) output.push_back(point);
}

void AppendTail(const CsrGraph& graph,
                const RoadCandidate& candidate,
                std::vector<Coordinate>& output) {
    // Start route: projected point -> remaining directed edge -> target node.
    Append(output, candidate.projected);
    const auto end = graph.geometry_offsets[candidate.edge + 1];
    for (std::uint64_t point = candidate.geometry_index + 1; point < end; ++point) {
        Append(output, graph.geometry[point]);
    }
}

void AppendHead(const CsrGraph& graph,
                const RoadCandidate& candidate,
                std::vector<Coordinate>& output) {
    // End route: source node -> directed edge prefix -> projected point.
    const auto begin = graph.geometry_offsets[candidate.edge];
    for (std::uint64_t point = begin; point <= candidate.geometry_index; ++point) {
        Append(output, graph.geometry[point]);
    }
    Append(output, candidate.projected);
}

double EdgeFraction(const CsrGraph& graph, const RoadCandidate& candidate) {
    const double length = graph.distance_m[candidate.edge];
    return length <= 0.0 ? 0.0 : std::clamp(candidate.distance_from_edge_start_m / length, 0.0, 1.0);
}

}  // namespace

RoutingEngine::RoutingEngine(RoadNetworkStore::Snapshot network) : network_(std::move(network)) {
    if (!network_) throw std::invalid_argument("RoutingEngine requires a road network snapshot");
}

std::vector<RoadCandidate> RoutingEngine::NearestRoads(
    Coordinate query, double radius_m, std::size_t maximum_candidates) const {
    return network_->GetSpatialIndex().Nearest(query, radius_m, maximum_candidates);
}

CoordinateRouteResult RoutingEngine::Route(const CoordinateRouteRequest& request) const {
    CoordinateRouteResult best;
    best.network_version = network_->Version();
    const auto starts = NearestRoads(request.source, request.snap_radius_m, request.snap_candidates);
    const auto ends = NearestRoads(request.target, request.snap_radius_m, request.snap_candidates);
    if (starts.empty() || ends.empty()) {
        best.status = RouteStatus::InvalidNode;
        return best;
    }

    double best_cost = std::numeric_limits<double>::infinity();
    // Evaluate candidate pairs because the geometrically closest directed edge
    // can be unusable due to oneway direction or disconnected components.
    for (const auto& start : starts) {
        for (const auto& end : ends) {
            CoordinateRouteResult candidate = RouteCandidates(start, end, request.search);
            if (candidate.status != RouteStatus::Ok) continue;
            // Snapping affects candidate selection but is not road distance.
            // Time mode converts off-network distance using walking speed.
            const double snap_distance = candidate.source_snap_distance_m +
                                         candidate.target_snap_distance_m;
            const double cost = request.search.metric == CostMetric::Distance
                                    ? candidate.distance_m + snap_distance
                                    : candidate.duration_s + snap_distance / 1.4;
            if (cost < best_cost) {
                best_cost = cost;
                best = std::move(candidate);
            }
        }
    }
    return best;
}

CoordinateRouteResult RoutingEngine::RouteCandidates(
    const RoadCandidate& start,
    const RoadCandidate& end,
    const SearchOptions& search) const {
    const auto& graph = network_->Graph();
    CoordinateRouteResult candidate;
    candidate.network_version = network_->Version();
    candidate.source_projected = start.projected;
    candidate.target_projected = end.projected;
    candidate.source_snap_distance_m = start.distance_m;
    candidate.target_snap_distance_m = end.distance_m;
    if (start.edge >= graph.EdgeCount() || end.edge >= graph.EdgeCount()) {
        candidate.status = RouteStatus::InvalidNode;
        return candidate;
    }

    const double start_fraction = EdgeFraction(graph, start);
    const double end_fraction = EdgeFraction(graph, end);
    // Same directed edge needs no graph search if the target projection lies
    // after the source projection in legal travel direction.
    if (start.edge == end.edge && start_fraction <= end_fraction) {
        const double fraction = end_fraction - start_fraction;
        candidate.distance_m = graph.distance_m[start.edge] * fraction;
        candidate.duration_s = graph.duration_s[start.edge] * fraction;
        Append(candidate.geometry, start.projected);
        for (std::uint64_t point = start.geometry_index + 1;
             point <= end.geometry_index; ++point) {
            Append(candidate.geometry, graph.geometry[point]);
        }
        Append(candidate.geometry, end.projected);
        candidate.status = RouteStatus::Ok;
        return candidate;
    }

    // Virtual-edge model: pay the start suffix, route between real topology
    // nodes, then pay the destination prefix.
    const NodeId route_source = graph.targets[start.edge];
    const NodeId route_target = EdgeSource(graph, end.edge);
    const auto core = network_->GetRouter().Route(route_source, route_target, search);
    if (core.status != RouteStatus::Ok) {
        candidate.status = core.status;
        return candidate;
    }
    candidate.distance_m = graph.distance_m[start.edge] * (1.0 - start_fraction) +
                           core.distance_m + graph.distance_m[end.edge] * end_fraction;
    candidate.duration_s = graph.duration_s[start.edge] * (1.0 - start_fraction) +
                           core.duration_s + graph.duration_s[end.edge] * end_fraction;
    candidate.statistics = core.statistics;
    AppendTail(graph, start, candidate.geometry);
    for (const auto& point : core.geometry) Append(candidate.geometry, point);
    AppendHead(graph, end, candidate.geometry);
    candidate.status = RouteStatus::Ok;
    return candidate;
}

}  // namespace routing
