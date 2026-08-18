#include "routing/csr_graph.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace routing {

void CsrGraph::Validate() const {
    // Validate parallel-array invariants centrally. Loaders, builders and
    // RoadNetwork construction all call this before exposing a snapshot.
    if (node_osm_ids.size() != node_coordinates.size()) {
        throw std::runtime_error("CSR node metadata size mismatch");
    }
    if (offsets.size() != NodeCount() + 1 || offsets.empty() || offsets.front() != 0 ||
        offsets.back() != EdgeCount()) {
        throw std::runtime_error("CSR offsets are inconsistent");
    }
    if (!std::is_sorted(offsets.begin(), offsets.end())) {
        throw std::runtime_error("CSR offsets are not monotonic");
    }
    if (distance_m.size() != EdgeCount() || duration_s.size() != EdgeCount() ||
        edge_osm_way_ids.size() != EdgeCount()) {
        throw std::runtime_error("CSR edge metadata size mismatch");
    }
    if (geometry_offsets.size() != EdgeCount() + 1 || geometry_offsets.empty() ||
        geometry_offsets.front() != 0 || geometry_offsets.back() != geometry.size() ||
        !std::is_sorted(geometry_offsets.begin(), geometry_offsets.end())) {
        throw std::runtime_error("CSR geometry offsets are inconsistent");
    }
    for (const NodeId target : targets) {
        if (target >= NodeCount()) throw std::runtime_error("CSR target is out of range");
    }
}

CsrGraph CsrBuilder::Build(const RawGraph& raw) const {
    if (raw.nodes.size() > std::numeric_limits<NodeId>::max() ||
        raw.edges.size() > std::numeric_limits<EdgeId>::max()) {
        throw std::runtime_error("Raw graph exceeds 32-bit dense ID capacity");
    }

    // Dense IDs replace sparse 64-bit OSM IDs in all hot search arrays. The
    // original IDs remain available for diagnostics and data reconciliation.
    CsrGraph graph;
    graph.node_osm_ids.reserve(raw.nodes.size());
    graph.node_coordinates.reserve(raw.nodes.size());
    std::unordered_map<OsmNodeId, NodeId> dense_ids;
    dense_ids.reserve(raw.nodes.size());
    for (const auto& node : raw.nodes) {
        const NodeId id = static_cast<NodeId>(graph.node_osm_ids.size());
        if (!dense_ids.emplace(node.osm_id, id).second) {
            throw std::runtime_error("Duplicate RawGraph OSM node ID");
        }
        graph.node_osm_ids.push_back(node.osm_id);
        graph.node_coordinates.push_back(node.coordinate);
    }

    // CSR requires all outgoing edges of a node to be contiguous. Keep RawGraph
    // immutable and sort an index vector rather than copying rich RawGraphEdge.
    std::vector<std::size_t> order(raw.edges.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        const auto lhs_source = dense_ids.at(raw.edges[lhs].source);
        const auto rhs_source = dense_ids.at(raw.edges[rhs].source);
        if (lhs_source != rhs_source) return lhs_source < rhs_source;
        return dense_ids.at(raw.edges[lhs].target) < dense_ids.at(raw.edges[rhs].target);
    });

    graph.offsets.assign(raw.nodes.size() + 1, 0);
    graph.targets.reserve(raw.edges.size());
    graph.distance_m.reserve(raw.edges.size());
    graph.duration_s.reserve(raw.edges.size());
    graph.edge_osm_way_ids.reserve(raw.edges.size());
    graph.geometry_offsets.reserve(raw.edges.size() + 1);
    graph.geometry_offsets.push_back(0);

    for (const std::size_t raw_index : order) {
        const auto& edge = raw.edges[raw_index];
        const auto source = dense_ids.find(edge.source);
        const auto target = dense_ids.find(edge.target);
        if (source == dense_ids.end() || target == dense_ids.end()) {
            throw std::runtime_error("RawGraph edge references a missing topology node");
        }
        ++graph.offsets[source->second + 1];
        graph.targets.push_back(target->second);
        graph.distance_m.push_back(static_cast<float>(edge.distance_m));
        graph.duration_s.push_back(static_cast<float>(edge.duration_s));
        graph.edge_osm_way_ids.push_back(edge.osm_way_id);
        graph.geometry.insert(graph.geometry.end(), edge.geometry.begin(), edge.geometry.end());
        graph.geometry_offsets.push_back(graph.geometry.size());
    }
    // Counts stored at source+1 become exclusive prefix offsets.
    std::partial_sum(graph.offsets.begin(), graph.offsets.end(), graph.offsets.begin());
    graph.Validate();
    return graph;
}

}  // namespace routing
