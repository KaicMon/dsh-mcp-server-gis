#include "routing/topology_builder.h"

#include <algorithm>
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

std::filesystem::path Fixture(const std::string& name) {
    return std::filesystem::path{ROUTING_TEST_DATA_DIR} / name;
}

bool HasEdge(const routing::RawGraph& graph,
             routing::OsmNodeId source,
             routing::OsmNodeId target) {
    return std::any_of(graph.edges.begin(), graph.edges.end(), [&](const auto& edge) {
        return edge.source == source && edge.target == target;
    });
}

}  // namespace

int main() {
    const routing::TopologyBuilder builder;

    const auto cross = builder.Build(Fixture("simple_cross.osm"));
    Require(cross.nodes.size() == 5, "cross topology node count");
    Require(cross.edges.size() == 8, "cross directed edge count");
    Require(HasEdge(cross, 1, 2) && HasEdge(cross, 2, 1), "cross bidirectional edge");
    Require(HasEdge(cross, 2, 5) && HasEdge(cross, 5, 2), "intersection split");
    Require(cross.edges.front().distance_m > 0.0, "edge distance");
    Require(cross.edges.front().duration_s > 0.0, "edge duration");
    Require(cross.edges.front().geometry.size() == 2, "split geometry");

    const auto oneway = builder.Build(Fixture("one_way.osm"));
    Require(oneway.nodes.size() == 3, "oneway shape-node compression");
    Require(oneway.edges.size() == 2, "oneway directed edge count");
    Require(HasEdge(oneway, 11, 13), "forward oneway");
    Require(!HasEdge(oneway, 13, 11), "no reverse edge for forward oneway");
    Require(HasEdge(oneway, 15, 13), "oneway=-1 reverses node order");
    Require(!HasEdge(oneway, 13, 15), "no forward edge for oneway=-1");

    const auto roundabout = builder.Build(Fixture("roundabout.osm"));
    Require(roundabout.nodes.size() == 4, "roundabout topology nodes");
    Require(roundabout.edges.size() == 6, "roundabout and connector edge count");
    Require(HasEdge(roundabout, 21, 22), "roundabout forward edge");
    const auto roundabout_edge_count = std::count_if(
        roundabout.edges.begin(), roundabout.edges.end(),
        [](const auto& edge) { return edge.osm_way_id == 121; });
    Require(roundabout_edge_count == 2, "roundabout emits one edge per forward segment");
    const auto long_arc = std::find_if(
        roundabout.edges.begin(), roundabout.edges.end(), [](const auto& edge) {
            return edge.osm_way_id == 121 && edge.source == 22 && edge.target == 21;
        });
    Require(long_arc != roundabout.edges.end() && long_arc->geometry.size() == 4,
            "roundabout keeps forward long-arc geometry");

    const auto disconnected = builder.Build(Fixture("disconnected.osm"));
    Require(disconnected.nodes.size() == 4, "disconnected topology nodes");
    Require(disconnected.edges.size() == 4, "disconnected directed edges");

    std::cout << "topology_builder_test passed\n";
    return 0;
}
