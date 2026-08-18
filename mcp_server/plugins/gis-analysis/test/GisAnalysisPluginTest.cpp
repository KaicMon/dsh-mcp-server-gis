#include "PluginAPI.h"
#include "json.hpp"

#include <dlfcn.h>

#include <cstdlib>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

json Call(PluginAPI& plugin, const char* name, json arguments) {
    const json request = {
        {"jsonrpc", "2.0"},
        {"id", "plugin-test"},
        {"method", "tools/call"},
        {"params", {{"name", name}, {"arguments", std::move(arguments)}}},
    };
    char* raw = plugin.HandleRequest(request.dump().c_str());
    if (raw == nullptr) {
        throw std::runtime_error("plugin returned a null result");
    }
    const json result = json::parse(raw);
    plugin.FreeResult(raw);
    return result;
}

}  // namespace

int main() {
    // Dataset tools are intentionally sandboxed. Point the plugin at a small,
    // deterministic fixture before Initialize reads its environment.
    setenv("GIS_DATASET_ROOT", GIS_ANALYSIS_TEST_DATA_DIR, 1);
    // Load through the real C ABI rather than linking the plugin into the test.
    // This catches missing exports and cross-module result ownership mistakes.
    void* library = dlopen(GIS_ANALYSIS_PLUGIN_PATH, RTLD_NOW | RTLD_LOCAL);
    const char* load_error = library == nullptr ? dlerror() : nullptr;
    if (!Expect(library != nullptr,
                load_error == nullptr ? "dlopen failed" : load_error)) {
        return 1;
    }

    auto create = reinterpret_cast<PluginAPI* (*)()>(dlsym(library, "CreatePlugin"));
    auto destroy = reinterpret_cast<void (*)(PluginAPI*)>(dlsym(library, "DestroyPlugin"));
    bool ok = true;
    ok &= Expect(create != nullptr, "CreatePlugin export must exist");
    ok &= Expect(destroy != nullptr, "DestroyPlugin export must exist");
    if (!ok) {
        dlclose(library);
        return 1;
    }

    PluginAPI* plugin = create();
    ok &= Expect(plugin != nullptr, "CreatePlugin must return an API table");
    ok &= Expect(plugin != nullptr && plugin->abiVersion == MCP_PLUGIN_ABI_VERSION,
                 "plugin ABI version must match the host");
    ok &= Expect(plugin != nullptr && plugin->Initialize() == 1,
                 "plugin initialization must succeed");
    ok &= Expect(plugin != nullptr && plugin->GetToolCount() == 8,
                 "plugin generation must expose eight GIS tools");

    if (plugin != nullptr && ok) {
        const json valid = Call(*plugin, "geometry_validate",
            {{"geometry", {{"type", "Point"},
                            {"coordinates", json::array({118.7969, 32.0603})}}}});
        ok &= Expect(valid.at("isError") == false,
                     "valid geometry call must succeed");
        ok &= Expect(valid.at("structuredContent").at("schemaVersion") == "1.0",
                     "success must use the versioned GIS envelope");
        ok &= Expect(valid.at("structuredContent").at("traceId") == "mcp_plugin-test",
                     "GIS envelope must correlate the MCP request ID");

        const json transformed = Call(*plugin, "coordinate_transform",
            {{"coordinate", {{"x", 118.7969}, {"y", 32.0603}}},
             {"sourceCrs", "EPSG:4326"}, {"targetCrs", "EPSG:3857"}});
        ok &= Expect(transformed.at("isError") == false,
                     "coordinate transformation must succeed");
        ok &= Expect(transformed.at("structuredContent").at("data")
                         .at("coordinate").at("x").get<double>() > 13'000'000.0,
                     "longitude must be transformed as the x axis");

        const json invalid = Call(*plugin, "geometry_validate",
            {{"geometry", {{"type", "Polygon"},
                            {"coordinates", json::array({json::array({
                                json::array({0, 0}), json::array({2, 2}),
                                json::array({0, 2}), json::array({2, 0}),
                                json::array({0, 0})})})}}}});
        ok &= Expect(invalid.at("isError") == true,
                     "invalid topology must return an MCP tool error");
        ok &= Expect(invalid.at("structuredContent").at("error").at("code") ==
                         "INVALID_GEOMETRY",
                     "invalid topology must use a stable error code");

        const json point = {{"type", "Point"},
                            {"coordinates", json::array({118.80, 32.06})}};
        const json polygon = {{"type", "Polygon"},
            {"coordinates", json::array({json::array({
                json::array({118.79, 32.05}), json::array({118.81, 32.05}),
                json::array({118.81, 32.07}), json::array({118.79, 32.07}),
                json::array({118.79, 32.05})})})}};
        const json contains = Call(*plugin, "point_in_polygon",
                                   {{"polygon", polygon}, {"point", point}});
        ok &= Expect(contains.at("structuredContent").at("data").at("contains") == true,
                     "point_in_polygon must report an interior point");

        const json area = Call(*plugin, "geometry_area", {{"geometry", polygon}});
        ok &= Expect(area.at("isError") == false &&
                         area.at("structuredContent").at("data")
                             .at("areaSquareMeters").get<double>() > 3'000'000.0,
                     "geometry_area must return metric polygon area");

        const json buffer = Call(*plugin, "geometry_buffer",
            {{"geometry", point}, {"distanceMeters", 1000.0}});
        const auto& buffer_visualization =
            buffer.at("structuredContent").at("visualization");
        const char* external_value = std::getenv("EXPECT_EXTERNAL_RESULT");
        const bool expect_external = external_value != nullptr &&
                                     std::string_view(external_value) == "1";
        ok &= Expect(buffer.at("isError") == false &&
                         (expect_external
                              ? (!buffer_visualization.at("inline").get<bool>() &&
                                 buffer_visualization.at("resultId").is_string())
                              : buffer_visualization.at("inline").get<bool>()),
                     "geometry_buffer must follow the configured output policy");

        const json missing_buffer_arguments =
            Call(*plugin, "geometry_buffer", json::object());
        ok &= Expect(missing_buffer_arguments.at("isError") == true &&
                         missing_buffer_arguments.at("structuredContent").at("error")
                             .at("code") == "MISSING_REQUIRED_PARAMETER" &&
                         missing_buffer_arguments.at("structuredContent").at("error")
                             .at("message").get<std::string>().find("geometry") !=
                             std::string::npos &&
                         missing_buffer_arguments.at("structuredContent").at("error")
                             .at("message").get<std::string>().find("distanceMeters") !=
                             std::string::npos,
                     "geometry_buffer must report all missing required parameters");

        const json dataset = Call(*plugin, "dataset_info", {{"path", "places.geojson"}});
        ok &= Expect(dataset.at("isError") == false &&
                         dataset.at("structuredContent").at("data").at("driver") ==
                             "GeoJSON" &&
                         dataset.at("structuredContent").at("data").at("layers")
                             .at(0).at("featureCount") == 3,
                     "dataset_info must expose GDAL metadata inside the dataset root");

        const json query_polygon = {{"type", "Polygon"},
            {"coordinates", json::array({json::array({
                json::array({118.79, 32.05}), json::array({118.81, 32.05}),
                json::array({118.81, 32.07}), json::array({118.79, 32.07}),
                json::array({118.79, 32.05})})})}};
        const json selected = Call(*plugin, "features_within",
            {{"path", "places.geojson"}, {"polygon", query_polygon}, {"limit", 10}});
        const auto& selected_visualization =
            selected.at("structuredContent").at("visualization");
        ok &= Expect(selected.at("isError") == false &&
                         selected.at("structuredContent").at("data")
                             .at("featureCount") == 2 &&
                         (expect_external
                              ? (!selected_visualization.at("inline").get<bool>() &&
                                 selected_visualization.at("resultId").is_string())
                              : selected_visualization.at("inline").get<bool>()),
                     "features_within must spatially filter and return GeoJSON");

        const json escaped = Call(*plugin, "dataset_info",
                                  {{"path", "../CMakeLists.txt"}});
        ok &= Expect(escaped.at("isError") == true &&
                         escaped.at("structuredContent").at("error").at("code") ==
                             "DATASET_ACCESS_DENIED",
                     "dataset tools must reject paths outside GIS_DATASET_ROOT");
    }

    if (plugin != nullptr) {
        plugin->Shutdown();
        destroy(plugin);
    }
    ok &= Expect(dlclose(library) == 0, "plugin library must unload cleanly");
    return ok ? 0 : 1;
}
