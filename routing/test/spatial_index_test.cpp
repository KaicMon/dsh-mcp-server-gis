#include "routing/spatial_index.h"
#include "routing/topology_builder.h"

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
}  // namespace

int main() {
    const auto path = std::filesystem::path{ROUTING_TEST_DATA_DIR} / "simple_cross.osm";
    const auto graph = routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(path));
    const routing::SpatialIndex index{graph, 0.001};
    const auto candidates = index.Nearest({116.0004, 40.0001}, 100.0, 4);
    Require(!candidates.empty(), "nearby road candidate");
    Require(candidates.front().distance_m < 20.0, "projection distance");
    Require(candidates.front().projected.latitude > 39.99999 &&
                candidates.front().projected.latitude < 40.00001,
            "projection coordinate");
    Require(index.Nearest({117.0, 41.0}, 100.0).empty(), "out-of-range query");
    std::cout << "spatial_index_test passed\n";
    return 0;
}
