#pragma once

#include "routing/network_store.h"

#include <cstddef>
#include <limits>

namespace routing {

struct CoordinateRouteRequest {
    Coordinate source;
    Coordinate target;
    double snap_radius_m = 1000.0;
    std::size_t snap_candidates = 4;
    SearchOptions search;
};

struct CoordinateRouteResult {
    RouteStatus status = RouteStatus::Unreachable;
    double distance_m = 0.0;
    double duration_s = 0.0;
    Coordinate source_projected;
    Coordinate target_projected;
    double source_snap_distance_m = 0.0;
    double target_snap_distance_m = 0.0;
    std::vector<Coordinate> geometry;
    SearchStatistics statistics;
    std::string network_version;
};

class RoutingEngine {
public:
    // Captures one immutable RoadNetwork generation. A request can therefore
    // never observe a version switch halfway through its route calculation.
    explicit RoutingEngine(RoadNetworkStore::Snapshot network);

    // Routes between arbitrary WGS84 positions. Candidate directed edges are
    // evaluated with partial-edge costs, not rounded to their nearest graph node.
    [[nodiscard]] CoordinateRouteResult Route(const CoordinateRouteRequest& request) const;

    // Routes between two already-selected directed-edge states. Map matching
    // uses this to evaluate a Viterbi transition without re-snapping either GPS
    // observation to a different road candidate.
    [[nodiscard]] CoordinateRouteResult RouteCandidates(
        const RoadCandidate& source,
        const RoadCandidate& target,
        const SearchOptions& search = {}) const;

    // Exposes the same spatial candidate semantics used internally by Route().
    [[nodiscard]] std::vector<RoadCandidate> NearestRoads(
        Coordinate query, double radius_m, std::size_t maximum_candidates = 8) const;
    [[nodiscard]] const RoadNetwork& Network() const noexcept { return *network_; }

private:
    RoadNetworkStore::Snapshot network_;
};

}  // namespace routing
