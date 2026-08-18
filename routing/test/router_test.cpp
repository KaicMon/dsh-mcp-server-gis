#include "routing/router.h"
#include "routing/topology_builder.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

routing::CsrGraph LoadFixture(const std::string& name) {
    const auto path = std::filesystem::path{ROUTING_TEST_DATA_DIR} / name;
    return routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(path));
}

routing::NodeId FindNode(const routing::CsrGraph& graph, routing::OsmNodeId osm_id) {
    for (routing::NodeId id = 0; id < graph.NodeCount(); ++id) {
        if (graph.node_osm_ids[id] == osm_id) return id;
    }
    return std::numeric_limits<routing::NodeId>::max();
}

}  // namespace

int main() {
    const auto cross = LoadFixture("simple_cross.osm");
    const routing::Router cross_router{cross};
    const auto source = FindNode(cross, 1);
    const auto target = FindNode(cross, 5);
    const auto dijkstra = cross_router.Route(
        source, target, {routing::SearchAlgorithm::Dijkstra, routing::CostMetric::Duration});
    const auto astar = cross_router.Route(
        source, target, {routing::SearchAlgorithm::AStar, routing::CostMetric::Duration});
    const auto bidirectional = cross_router.Route(
        source, target,
        {routing::SearchAlgorithm::BidirectionalDijkstra, routing::CostMetric::Duration});
    Require(dijkstra.status == routing::RouteStatus::Ok, "Dijkstra route status");
    Require(astar.status == routing::RouteStatus::Ok, "A* route status");
    Require(std::abs(dijkstra.duration_s - astar.duration_s) < 1e-5,
            "A* and Dijkstra optimal duration");
    Require(bidirectional.status == routing::RouteStatus::Ok,
            "bidirectional Dijkstra route status");
    Require(std::abs(dijkstra.duration_s - bidirectional.duration_s) < 1e-5,
            "bidirectional and Dijkstra optimal duration");
    Require(dijkstra.edges.size() == 2, "route edge reconstruction");
    Require(dijkstra.geometry.size() == 3, "route geometry reconstruction");

    const auto oneway = LoadFixture("one_way.osm");
    const routing::Router oneway_router{oneway};
    Require(oneway_router.Route(FindNode(oneway, 11), FindNode(oneway, 15)).status ==
                routing::RouteStatus::Unreachable,
            "oneway chain blocks forward continuation");
    Require(oneway_router.Route(FindNode(oneway, 15), FindNode(oneway, 13)).status ==
                routing::RouteStatus::Ok,
            "oneway=-1 route");

    const auto disconnected = LoadFixture("disconnected.osm");
    const routing::Router disconnected_router{disconnected};
    Require(disconnected_router.Route(FindNode(disconnected, 41), FindNode(disconnected, 44)).status ==
                routing::RouteStatus::Unreachable,
            "disconnected route");
    Require(disconnected_router.Route(disconnected.NodeCount(), 0).status ==
                routing::RouteStatus::InvalidNode,
            "invalid node route");

    std::cout << "router_test passed\n";
    return 0;
}
