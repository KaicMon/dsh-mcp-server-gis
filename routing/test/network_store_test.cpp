#include "routing/network_store.h"
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

std::filesystem::path BuildFile(const std::string& fixture, const std::string& suffix) {
    const auto input = std::filesystem::path{ROUTING_TEST_DATA_DIR} / fixture;
    const auto output = std::filesystem::temp_directory_path() / suffix;
    const auto graph = routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(input));
    routing::NetworkFile::Save(graph, output);
    return output;
}
}  // namespace

int main() {
    const auto first_file = BuildFile("simple_cross.osm", "network-store-v1.route");
    const auto second_file = BuildFile("disconnected.osm", "network-store-v2.route");
    routing::RoadNetworkStore store;
    auto first_candidate = store.Stage(first_file, "v1");
    Require(!store.Current(), "staging does not publish");
    store.Publish(first_candidate);
    auto old_request_snapshot = store.Current();
    Require(old_request_snapshot->Version() == "v1", "first publish");

    store.Publish(store.Stage(second_file, "v2"));
    Require(store.Current()->Version() == "v2", "second publish");
    Require(old_request_snapshot->Version() == "v1", "old request retains old generation");
    Require(old_request_snapshot.use_count() >= 2, "old generation remains alive");
    Require(store.Rollback(), "rollback succeeds");
    Require(store.Current()->Version() == "v1", "rollback restores version");

    std::filesystem::remove(first_file);
    std::filesystem::remove(second_file);
    std::cout << "network_store_test passed\n";
    return 0;
}
