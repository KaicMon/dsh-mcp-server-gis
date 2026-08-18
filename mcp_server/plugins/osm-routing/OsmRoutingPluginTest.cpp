#include "PluginAPI.h"
#include "json.hpp"
#include "routing/csr_graph.h"
#include "routing/topology_builder.h"

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

using json = nlohmann::json;

int main() {
    const auto fixture = std::filesystem::temp_directory_path() /
                         "osm-routing-plugin-test.route";
    routing::NetworkFile::Save(
        routing::CsrBuilder{}.Build(routing::TopologyBuilder{}.Build(
            std::filesystem::path{ROUTING_TEST_DATA_DIR} / "simple_cross.osm")),
        fixture);
    setenv("MCP_ROUTING_NETWORK", fixture.c_str(), 1);
    setenv("MCP_ROUTING_NETWORK_VERSION", "abi-fixture-v1", 1);

    bool ok = true;
    void* library = dlopen(OSM_ROUTING_PLUGIN_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        std::cerr << dlerror() << '\n';
        return 1;
    }
    auto create = reinterpret_cast<PluginAPI* (*)()>(dlsym(library, "CreatePlugin"));
    auto destroy = reinterpret_cast<void (*)(PluginAPI*)>(dlsym(library, "DestroyPlugin"));
    PluginAPI* plugin = create ? create() : nullptr;
    ok &= plugin && destroy && plugin->Initialize() == 1;
    if (ok) {
        const json request{{"jsonrpc", "2.0"}, {"id", "routing-abi"},
            {"method", "tools/call"}, {"params", {{"name", "route_plan"},
             {"arguments", {{"source", {{"longitude", 115.9992}, {"latitude", 40.00005}}},
                            {"target", {{"longitude", 116.00005}, {"latitude", 40.0008}}}}}}}};
        char* raw = plugin->HandleRequest(request.dump().c_str());
        const json response = json::parse(raw);
        plugin->FreeResult(raw);
        const auto& structured = response.at("structuredContent");
        ok &= !response.at("isError").get<bool>() &&
              structured.at("schemaVersion") == "1.0" &&
              structured.at("traceId") == "mcp_routing-abi" &&
              structured.at("visualization").at("geojson").at("type") == "LineString";
        plugin->Shutdown();
        destroy(plugin);
    }
    dlclose(library);
    unsetenv("MCP_ROUTING_NETWORK");
    unsetenv("MCP_ROUTING_NETWORK_VERSION");
    std::filesystem::remove(fixture);
    return ok ? 0 : 1;
}
