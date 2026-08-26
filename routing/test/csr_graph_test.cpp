#include "routing/csr_graph.h"
#include "routing/topology_builder.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const auto fixture = std::filesystem::path{ROUTING_TEST_DATA_DIR} / "simple_cross.osm";
    const auto raw = routing::TopologyBuilder{}.Build(fixture);
    const auto graph = routing::CsrBuilder{}.Build(raw);
    graph.Validate();
    Require(graph.NodeCount() == 5, "dense node count");
    Require(graph.EdgeCount() == 8, "CSR edge count");
    Require(graph.offsets.size() == 6, "CSR offset count");

    // C++20 views keep CSR slices non-owning: the data pointer is the graph's
    // compact backing store rather than a per-query copy.
    const auto outgoing = graph.OutgoingTargets(0);
    Require(!outgoing.empty(), "outgoing target view should expose CSR slice");
    Require(outgoing.data() == graph.targets.data() + graph.offsets[0],
            "outgoing target view should not copy CSR targets");
    const auto edge_geometry = graph.EdgeGeometry(0);
    Require(!edge_geometry.empty(), "edge geometry view should expose shape points");
    Require(edge_geometry.data() == graph.geometry.data() + graph.geometry_offsets[0],
            "edge geometry view should not copy CSR geometry");
    Require(graph.geometry_offsets.size() == 9, "geometry offset count");

    const auto temp = std::filesystem::temp_directory_path() / "routing-csr-test.route";
    routing::NetworkFile::Save(graph, temp);
    const auto loaded = routing::NetworkFile::Load(temp);
    std::filesystem::remove(temp);
    Require(loaded.node_osm_ids == graph.node_osm_ids, "node IDs round trip");
    Require(loaded.offsets == graph.offsets, "offsets round trip");
    Require(loaded.targets == graph.targets, "targets round trip");
    Require(loaded.geometry.size() == graph.geometry.size(), "geometry round trip");

    std::cout << "csr_graph_test passed\n";
    return 0;
}
