#include "gis/providers/local_osm_provider.h"

#include "routing/csr_graph.h"
#include "routing/topology_builder.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

using namespace gis::providers;

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

}  // namespace

int main() {
    const auto input = std::filesystem::path{ROUTING_TEST_DATA_DIR} / "simple_cross.osm";
    const auto output = std::filesystem::temp_directory_path() /
                        "gis-local-osm-provider.route";
    routing::NetworkFile::Save(
        routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(input)), output);

    bool ok = true;
    try {
        LocalOsmProvider provider(output, "provider-fixture-v1");
        const auto health = provider.HealthCheck();
        ok &= Expect(health.healthy, "staged local network should be healthy");

        const auto driving = provider.PlanRoute({
            .source = {115.9992, 40.00005},
            .target = {116.00005, 40.0008},
            .profile = RouteProfile::Driving,
        });
        ok &= Expect(driving.Ok() && driving.value->distance_meters > 0.0,
                     "driving route should use routing_core");
        ok &= Expect(driving.value->geometry_geojson.find("LineString") != std::string::npos,
                     "local route should expose GeoJSON geometry");

        const auto walking = provider.PlanRoute({
            .source = {115.9992, 40.00005},
            .target = {116.00005, 40.0008},
            .profile = RouteProfile::Walking,
        });
        ok &= Expect(!walking.Ok() && walking.error.code == ProviderErrorCode::Unsupported,
                     "walking must not silently use the driving graph");
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        ok = false;
    }
    std::filesystem::remove(output);
    return ok ? 0 : 1;
}
