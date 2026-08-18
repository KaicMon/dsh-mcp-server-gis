#include "PluginAPI.h"
#include "httplib.h"
#include "json.hpp"
#include "routing/csr_graph.h"
#include "routing/topology_builder.h"

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

json Call(PluginAPI& plugin, const char* name, json arguments = json::object()) {
    const json request = {{"jsonrpc", "2.0"}, {"id", "map-test"},
        {"method", "tools/call"},
        {"params", {{"name", name}, {"arguments", std::move(arguments)}}}};
    char* raw = plugin.HandleRequest(request.dump().c_str());
    if (!raw) throw std::runtime_error("null plugin result");
    json result = json::parse(raw);
    plugin.FreeResult(raw);
    return result;
}

}  // namespace

int main() {
    // A loopback Amap double makes the compound Tool test deterministic and
    // verifies the real HTTP/provider/plugin path without external credentials.
    httplib::Server amap;
    int geocode_requests = 0;
    int nearby_requests = 0;
    amap.Get("/v3/geocode/geo", [&](const httplib::Request& request,
                                    httplib::Response& response) {
        ++geocode_requests;
        const bool ambiguous = request.has_param("address") &&
            request.get_param_value("address") == "ambiguous";
        json geocodes = json::array({{{"formatted_address", "南京市新街口"},
                                      {"location", "118.778,32.042"},
                                      {"adcode", "320104"}}});
        if (ambiguous) {
            geocodes.push_back({{"formatted_address", "另一个新街口"},
                                {"location", "118.700,32.000"},
                                {"adcode", "320100"}});
        }
        response.set_content(json({{"status", "1"}, {"geocodes", geocodes}}).dump(),
                             "application/json");
    });
    amap.Get("/v3/place/around", [&](const httplib::Request&,
                                     httplib::Response& response) {
        ++nearby_requests;
        response.set_content(json({{"status", "1"}, {"pois", json::array({
            {{"id", "poi-1"}, {"name", "南京景点"}, {"type", "风景名胜"},
             {"address", "测试路 1 号"}, {"location", "118.780,32.044"},
             {"distance", "320"}}
        })}}).dump(), "application/json");
    });
    const int amap_port = amap.bind_to_any_port("127.0.0.1");
    if (!Expect(amap_port > 0, "Amap test double must bind")) return 1;
    std::thread amap_thread([&] { amap.listen_after_bind(); });

    const auto fixture = std::filesystem::temp_directory_path() /
                         "gis-map-services-plugin.route";
    routing::NetworkFile::Save(
        routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(
            std::filesystem::path{ROUTING_TEST_DATA_DIR} / "simple_cross.osm")),
        fixture);
    unsetenv("AMAP_API_KEY");
    unsetenv("MCP_ROUTING_NETWORK");
    void* library = dlopen(MAP_SERVICES_PLUGIN_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!Expect(library != nullptr, "map-services must load")) {
        amap.stop();
        amap_thread.join();
        std::filesystem::remove(fixture);
        return 1;
    }
    auto create = reinterpret_cast<PluginAPI* (*)()>(dlsym(library, "CreatePlugin"));
    auto destroy = reinterpret_cast<void (*)(PluginAPI*)>(dlsym(library, "DestroyPlugin"));
    bool ok = Expect(create && destroy, "plugin exports must exist");
    PluginAPI* plugin = ok ? create() : nullptr;
    ok &= Expect(plugin && plugin->Initialize() == 1,
                 "plugin should initialize without an online key");
    ok &= Expect(plugin && plugin->GetToolCount() == 8,
                 "plugin should expose eight map service tools");
    if (plugin) {
        const json status = Call(*plugin, "map_provider_status");
        ok &= Expect(status.at("isError") == false &&
                         status.at("structuredContent").at("data")
                             .at("configured") == false,
                     "status must report missing provider configuration");
        ok &= Expect(status.at("structuredContent").at("traceId") == "mcp_map-test",
                     "result envelope must correlate the MCP request ID");
        const json unavailable = Call(*plugin, "geocode", {{"query", "南京"}});
        ok &= Expect(unavailable.at("isError") == true &&
                         unavailable.at("structuredContent").at("error")
                             .at("code") == "PROVIDER_UNAVAILABLE",
                     "online call without a key must fail predictably");
        const json compound_unavailable = Call(*plugin, "search_nearby_by_place",
            {{"place", "南京新街口"}, {"radiusMeters", 3000},
             {"keyword", "景点"}});
        ok &= Expect(compound_unavailable.at("isError") == true &&
                         compound_unavailable.at("structuredContent").at("tool") ==
                             "search_nearby_by_place" &&
                         compound_unavailable.at("structuredContent").at("error")
                             .at("code") == "PROVIDER_UNAVAILABLE",
                     "compound nearby Tool must be routed and fail predictably");
        // Reinitialize exactly as production does, this time with a staged
        // local graph, and exercise the full plugin ABI-to-routing path.
        plugin->Shutdown();
        setenv("AMAP_API_KEY", "test-key", 1);
        const std::string amap_base_url =
            "http://127.0.0.1:" + std::to_string(amap_port);
        setenv("AMAP_BASE_URL", amap_base_url.c_str(), 1);
        setenv("MCP_ROUTING_NETWORK", fixture.c_str(), 1);
        setenv("MCP_ROUTING_NETWORK_VERSION", "plugin-fixture-v1", 1);
        ok &= Expect(plugin->Initialize() == 1,
                     "plugin should initialize a configured local graph");
        const json local_status = Call(*plugin, "map_provider_status");
        ok &= Expect(local_status.at("structuredContent").at("data")
                         .at("providers").at("local-osm").at("configured") == true,
                     "status must report the local OSM provider");
        const json nearby = Call(*plugin, "search_nearby_by_place",
            {{"place", "南京新街口"}, {"radiusMeters", 3000},
             {"keyword", "景点"}});
        ok &= Expect(nearby.at("isError") == false &&
                         nearby.at("structuredContent").at("data").at("pois").size() == 1 &&
                         nearby.at("structuredContent").at("meta")
                             .at("providerOperations") == 2 &&
                         geocode_requests == 1 && nearby_requests == 1,
                     "compound nearby Tool must geocode then search exactly once");
        const json ambiguous = Call(*plugin, "search_nearby_by_place",
            {{"place", "ambiguous"}, {"keyword", "景点"}});
        ok &= Expect(ambiguous.at("isError") == true &&
                         ambiguous.at("structuredContent").at("error").at("code") ==
                             "AMBIGUOUS_LOCATION" &&
                         ambiguous.at("structuredContent").at("data")
                             .at("candidates").size() == 2 &&
                         nearby_requests == 1,
                     "ambiguous place must expose candidates without searching");
        const json route = Call(*plugin, "provider_route",
            {{"source", {{"longitude", 115.9992}, {"latitude", 40.00005}}},
             {"target", {{"longitude", 116.00005}, {"latitude", 40.0008}}},
             {"profile", "driving"}, {"provider", "local-osm"}});
        const bool expect_external = std::getenv("EXPECT_EXTERNAL_RESULT") != nullptr;
        const auto& visualization =
            route.at("structuredContent").at("visualization");
        ok &= Expect(route.at("isError") == false &&
                         route.at("structuredContent").at("meta").at("provider") ==
                             "local-osm" &&
                         (expect_external
                              ? (!visualization.at("inline").get<bool>() &&
                                 visualization.at("resultId").is_string())
                              : visualization.at("geojson").at("type") == "LineString"),
                     "local route must expose inline or external WGS84 GeoJSON");
        plugin->Shutdown();
        destroy(plugin);
    }
    ok &= Expect(dlclose(library) == 0, "plugin must unload cleanly");
    unsetenv("MCP_ROUTING_NETWORK");
    unsetenv("MCP_ROUTING_NETWORK_VERSION");
    unsetenv("AMAP_API_KEY");
    unsetenv("AMAP_BASE_URL");
    amap.stop();
    amap_thread.join();
    std::filesystem::remove(fixture);
    return ok ? 0 : 1;
}
