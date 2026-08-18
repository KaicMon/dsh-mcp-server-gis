#include "routing/map_matcher.h"
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
    const auto file = std::filesystem::temp_directory_path() / "map-matcher.route";
    routing::NetworkFile::Save(
        routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(input)), file);
    routing::RoadNetworkStore store;
    store.Publish(store.Stage(file, "map-match-fixture"));
    const routing::RoutingEngine engine{store.Current()};
    const routing::MapMatcher matcher{engine};
    const std::vector<routing::Coordinate> trace{
        {115.9992, 40.00003}, {115.9998, 39.99998},
        {116.00002, 40.0004}, {115.99997, 40.0008}};
    routing::MapMatchOptions options;
    options.search_radius_m = 50.0;
    const auto result = matcher.Match(trace, options);
    Require(result.status == routing::MapMatchStatus::Ok, "map match status");
    Require(result.observations.size() == trace.size(), "matched observation count");
    Require(!result.geometry.empty(), "matched route geometry");
    Require(result.network_version == "map-match-fixture", "map match version");
    Require(matcher.Match({}).status == routing::MapMatchStatus::EmptyTrace,
            "empty trace status");
    std::filesystem::remove(file);
    std::cout << "map_matcher_test passed\n";
    return 0;
}
