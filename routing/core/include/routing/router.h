#pragma once

#include "routing/csr_graph.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace routing {

enum class SearchAlgorithm { Dijkstra, AStar, BidirectionalDijkstra };
enum class CostMetric { Distance, Duration };
enum class RouteStatus { Ok, InvalidNode, Unreachable, LimitExceeded };

struct SearchOptions {
    SearchAlgorithm algorithm = SearchAlgorithm::AStar;
    CostMetric metric = CostMetric::Duration;
    std::uint64_t max_expanded_nodes = 2'000'000;
};

struct SearchStatistics {
    std::uint64_t expanded_nodes = 0;
    std::uint64_t relaxed_edges = 0;
};

struct RouteResult {
    RouteStatus status = RouteStatus::Unreachable;
    double distance_m = 0.0;
    double duration_s = 0.0;
    std::vector<NodeId> nodes;
    std::vector<EdgeId> edges;
    std::vector<Coordinate> geometry;
    SearchStatistics statistics;
};

class Router {
public:
    // Router borrows an immutable graph. The graph must outlive this object.
    // Reverse adjacency is built once here for bidirectional queries.
    explicit Router(const CsrGraph& graph);

    // Searches between dense graph nodes. SearchContext arrays are allocated
    // per request, so concurrent calls do not share mutable algorithm state.
    [[nodiscard]] RouteResult Route(NodeId source,
                                    NodeId target,
                                    const SearchOptions& options = {}) const;

private:
    // Bidirectional search walks outgoing CSR forward and the precomputed
    // incoming adjacency backward, meeting at the best proven node.
    [[nodiscard]] RouteResult RouteBidirectional(NodeId source,
                                                 NodeId target,
                                                 const SearchOptions& options) const;
    // Converts an edge sequence into totals and frontend-ready coordinates.
    void PopulateResult(RouteResult& result,
                        const std::vector<NodeId>& nodes,
                        const std::vector<EdgeId>& edges) const;
    // Admissible lower bound: great-circle distance, optionally divided by the
    // fastest speed present in this graph snapshot.
    [[nodiscard]] double Heuristic(NodeId node, NodeId target, CostMetric metric) const;

    const CsrGraph& graph_;
    double maximum_speed_mps_ = 1.0;
    std::vector<std::uint64_t> reverse_offsets_;
    std::vector<NodeId> reverse_sources_;
    std::vector<EdgeId> reverse_edges_;
};

}  // namespace routing
