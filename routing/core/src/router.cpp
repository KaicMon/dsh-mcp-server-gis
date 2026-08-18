#include "routing/router.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <utility>

namespace routing {
namespace {

constexpr NodeId kInvalidNode = std::numeric_limits<NodeId>::max();
constexpr EdgeId kInvalidEdge = std::numeric_limits<EdgeId>::max();

double HaversineMeters(const Coordinate& lhs, const Coordinate& rhs) {
    constexpr double kEarthRadiusMeters = 6371008.8;
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    const double lat1 = lhs.latitude * kDegreesToRadians;
    const double lat2 = rhs.latitude * kDegreesToRadians;
    const double delta_lat = (rhs.latitude - lhs.latitude) * kDegreesToRadians;
    const double delta_lon = (rhs.longitude - lhs.longitude) * kDegreesToRadians;
    const double sin_lat = std::sin(delta_lat / 2.0);
    const double sin_lon = std::sin(delta_lon / 2.0);
    const double a = sin_lat * sin_lat + std::cos(lat1) * std::cos(lat2) * sin_lon * sin_lon;
    return 2.0 * kEarthRadiusMeters * std::asin(std::min(1.0, std::sqrt(a)));
}

}  // namespace

Router::Router(const CsrGraph& graph) : graph_(graph) {
    graph_.Validate();
    // Build incoming adjacency as (source, original edge). Keeping the original
    // EdgeId means backward search can reconstruct forward-facing geometry.
    reverse_offsets_.assign(graph_.NodeCount() + 1, 0);
    for (const NodeId target : graph_.targets) ++reverse_offsets_[target + 1];
    std::partial_sum(reverse_offsets_.begin(), reverse_offsets_.end(), reverse_offsets_.begin());
    reverse_sources_.resize(graph_.EdgeCount());
    reverse_edges_.resize(graph_.EdgeCount());
    auto cursor = reverse_offsets_;
    for (NodeId source = 0; source < graph_.NodeCount(); ++source) {
        for (std::uint64_t edge = graph_.offsets[source]; edge < graph_.offsets[source + 1]; ++edge) {
            const NodeId target = graph_.targets[edge];
            const auto position = cursor[target]++;
            reverse_sources_[position] = source;
            reverse_edges_[position] = static_cast<EdgeId>(edge);
        }
    }
    for (std::size_t edge = 0; edge < graph_.EdgeCount(); ++edge) {
        if (graph_.duration_s[edge] > 0.0F) {
            maximum_speed_mps_ = std::max(
                maximum_speed_mps_,
                static_cast<double>(graph_.distance_m[edge] / graph_.duration_s[edge]));
        }
    }
    // Float persistence can round an edge speed slightly upward. A small margin
    // keeps the time heuristic admissible after serialization.
    maximum_speed_mps_ *= 1.000001;
}

double Router::Heuristic(NodeId node, NodeId target, CostMetric metric) const {
    const double straight_line = HaversineMeters(
        graph_.node_coordinates[node], graph_.node_coordinates[target]);
    return metric == CostMetric::Distance ? straight_line : straight_line / maximum_speed_mps_;
}

RouteResult Router::Route(NodeId source,
                          NodeId target,
                          const SearchOptions& options) const {
    RouteResult result;
    if (source >= graph_.NodeCount() || target >= graph_.NodeCount()) {
        result.status = RouteStatus::InvalidNode;
        return result;
    }
    if (source == target) {
        result.status = RouteStatus::Ok;
        result.nodes.push_back(source);
        result.geometry.push_back(graph_.node_coordinates[source]);
        return result;
    }
    if (options.algorithm == SearchAlgorithm::BidirectionalDijkstra) {
        return RouteBidirectional(source, target, options);
    }

    // cost is the best known g-score. previous_* forms a compact predecessor
    // tree and is populated only when an edge strictly improves that score.
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> cost(graph_.NodeCount(), infinity);
    std::vector<NodeId> previous_node(graph_.NodeCount(), kInvalidNode);
    std::vector<EdgeId> previous_edge(graph_.NodeCount(), kInvalidEdge);
    using QueueEntry = std::pair<double, NodeId>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;
    cost[source] = 0.0;
    queue.emplace(options.algorithm == SearchAlgorithm::AStar
                      ? Heuristic(source, target, options.metric)
                      : 0.0,
                  source);

    // Dijkstra and A* share the same loop; A* only adds h(node) to queue
    // priority. Stale entries are discarded instead of using decrease-key.
    while (!queue.empty()) {
        const auto [priority, node] = queue.top();
        queue.pop();
        const double expected = cost[node] +
            (options.algorithm == SearchAlgorithm::AStar
                 ? Heuristic(node, target, options.metric)
                 : 0.0);
        if (priority > expected + 1e-9) continue;
        if (++result.statistics.expanded_nodes > options.max_expanded_nodes) {
            result.status = RouteStatus::LimitExceeded;
            return result;
        }
        if (node == target) break;

        for (std::uint64_t index = graph_.offsets[node];
             index < graph_.offsets[node + 1]; ++index) {
            ++result.statistics.relaxed_edges;
            const auto edge = static_cast<EdgeId>(index);
            const NodeId next = graph_.targets[edge];
            const double weight = options.metric == CostMetric::Distance
                                      ? graph_.distance_m[edge]
                                      : graph_.duration_s[edge];
            const double candidate = cost[node] + weight;
            if (candidate >= cost[next]) continue;
            cost[next] = candidate;
            previous_node[next] = node;
            previous_edge[next] = edge;
            const double estimate = options.algorithm == SearchAlgorithm::AStar
                                        ? Heuristic(next, target, options.metric)
                                        : 0.0;
            queue.emplace(candidate + estimate, next);
        }
    }

    if (previous_edge[target] == kInvalidEdge) {
        result.status = RouteStatus::Unreachable;
        return result;
    }

    // Walk predecessors backward, then reverse once. This also provides the
    // dense node chain used by diagnostics and future turn-restriction state.
    for (NodeId node = target; node != source; node = previous_node[node]) {
        result.nodes.push_back(node);
        result.edges.push_back(previous_edge[node]);
    }
    result.nodes.push_back(source);
    std::reverse(result.nodes.begin(), result.nodes.end());
    std::reverse(result.edges.begin(), result.edges.end());

    PopulateResult(result, result.nodes, result.edges);
    result.status = RouteStatus::Ok;
    return result;
}

void Router::PopulateResult(RouteResult& result,
                            const std::vector<NodeId>&,
                            const std::vector<EdgeId>& edges) const {
    // Adjacent edge geometries share one topology coordinate. Skip that first
    // coordinate after the initial edge to avoid duplicate GeoJSON points.
    for (const EdgeId edge : edges) {
        result.distance_m += graph_.distance_m[edge];
        result.duration_s += graph_.duration_s[edge];
        const auto begin = graph_.geometry_offsets[edge];
        const auto end = graph_.geometry_offsets[edge + 1];
        for (std::uint64_t index = begin; index < end; ++index) {
            if (!result.geometry.empty() && index == begin) continue;
            result.geometry.push_back(graph_.geometry[index]);
        }
    }
}

RouteResult Router::RouteBidirectional(NodeId source,
                                       NodeId target,
                                       const SearchOptions& options) const {
    RouteResult result;
    const double infinity = std::numeric_limits<double>::infinity();
    const auto weight = [&](EdgeId edge) {
        return options.metric == CostMetric::Distance
                   ? static_cast<double>(graph_.distance_m[edge])
                   : static_cast<double>(graph_.duration_s[edge]);
    };
    std::vector<double> forward(graph_.NodeCount(), infinity);
    std::vector<double> backward(graph_.NodeCount(), infinity);
    std::vector<NodeId> previous_node(graph_.NodeCount(), kInvalidNode);
    std::vector<EdgeId> previous_edge(graph_.NodeCount(), kInvalidEdge);
    std::vector<NodeId> next_node(graph_.NodeCount(), kInvalidNode);
    std::vector<EdgeId> next_edge(graph_.NodeCount(), kInvalidEdge);
    using Entry = std::pair<double, NodeId>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> forward_queue;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> backward_queue;
    forward[source] = 0.0;
    backward[target] = 0.0;
    forward_queue.emplace(0.0, source);
    backward_queue.emplace(0.0, target);
    double best = infinity;
    NodeId meeting = kInvalidNode;

    // Once the two minimum queue keys cannot beat best, no undiscovered path
    // can improve the current meeting point (standard bidirectional Dijkstra
    // termination condition for non-negative weights).
    while (!forward_queue.empty() && !backward_queue.empty()) {
        if (forward_queue.top().first + backward_queue.top().first >= best) break;
        // Expanding the cheaper frontier tends to keep both search balls
        // balanced without changing correctness.
        const bool expand_forward = forward_queue.top().first <= backward_queue.top().first;
        auto& queue = expand_forward ? forward_queue : backward_queue;
        const auto [distance, node] = queue.top();
        queue.pop();
        const auto& own_distance = expand_forward ? forward : backward;
        if (distance > own_distance[node]) continue;
        if (++result.statistics.expanded_nodes > options.max_expanded_nodes) {
            result.status = RouteStatus::LimitExceeded;
            return result;
        }
        if (forward[node] + backward[node] < best) {
            best = forward[node] + backward[node];
            meeting = node;
        }

        if (expand_forward) {
            for (std::uint64_t index = graph_.offsets[node]; index < graph_.offsets[node + 1]; ++index) {
                ++result.statistics.relaxed_edges;
                const EdgeId edge = static_cast<EdgeId>(index);
                const NodeId next = graph_.targets[edge];
                const double candidate = distance + weight(edge);
                if (candidate < forward[next]) {
                    forward[next] = candidate;
                    previous_node[next] = node;
                    previous_edge[next] = edge;
                    forward_queue.emplace(candidate, next);
                }
                if (candidate + backward[next] < best) {
                    best = candidate + backward[next];
                    meeting = next;
                }
            }
        } else {
            for (std::uint64_t index = reverse_offsets_[node]; index < reverse_offsets_[node + 1]; ++index) {
                ++result.statistics.relaxed_edges;
                const NodeId previous = reverse_sources_[index];
                const EdgeId edge = reverse_edges_[index];
                const double candidate = distance + weight(edge);
                if (candidate < backward[previous]) {
                    backward[previous] = candidate;
                    next_node[previous] = node;
                    next_edge[previous] = edge;
                    backward_queue.emplace(candidate, previous);
                }
                if (forward[previous] + candidate < best) {
                    best = forward[previous] + candidate;
                    meeting = previous;
                }
            }
        }
    }

    if (meeting == kInvalidNode || !std::isfinite(best)) {
        result.status = RouteStatus::Unreachable;
        return result;
    }
    // Reconstruct source -> meeting from forward predecessors, then meeting ->
    // target from next pointers recorded by the reverse traversal.
    for (NodeId node = meeting; node != source; node = previous_node[node]) {
        if (node == kInvalidNode || previous_edge[node] == kInvalidEdge) {
            result.status = RouteStatus::Unreachable;
            return result;
        }
        result.nodes.push_back(node);
        result.edges.push_back(previous_edge[node]);
    }
    result.nodes.push_back(source);
    std::reverse(result.nodes.begin(), result.nodes.end());
    std::reverse(result.edges.begin(), result.edges.end());
    for (NodeId node = meeting; node != target; node = next_node[node]) {
        if (node == kInvalidNode || next_edge[node] == kInvalidEdge) {
            result.status = RouteStatus::Unreachable;
            return result;
        }
        result.edges.push_back(next_edge[node]);
        result.nodes.push_back(next_node[node]);
    }
    PopulateResult(result, result.nodes, result.edges);
    result.status = RouteStatus::Ok;
    return result;
}

}  // namespace routing
