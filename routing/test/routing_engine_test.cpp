#include "routing/routing_engine.h"
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
    const auto input = std::filesystem::path{ROUTING_TEST_DATA_DIR} / "simple_cross.osm";
    const auto file = std::filesystem::temp_directory_path() / "routing-engine.route";
    routing::NetworkFile::Save(
        routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(input)), file);
    routing::RoadNetworkStore store;
    store.Publish(store.Stage(file, "fixture-v1"));
    const routing::RoutingEngine engine{store.Current()};
    routing::CoordinateRouteRequest request;
    request.source = {115.9992, 40.00005};
    request.target = {116.00005, 40.0008};
    request.snap_radius_m = 100.0;
    const auto result = engine.Route(request);
    Require(result.status == routing::RouteStatus::Ok, "coordinate route");
    Require(result.distance_m > 0.0, "coordinate route distance");
    Require(result.geometry.size() >= 3, "coordinate route geometry");
    Require(result.network_version == "fixture-v1", "route version");
    Require(result.source_snap_distance_m < 10.0, "source projection");
    Require(result.target_snap_distance_m < 10.0, "target projection");
    std::filesystem::remove(file);
    std::cout << "routing_engine_test passed\n";
    return 0;
}
