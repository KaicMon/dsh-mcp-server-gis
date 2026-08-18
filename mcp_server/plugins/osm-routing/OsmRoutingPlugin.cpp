#include "PluginAPI.h"
#include "json.hpp"
#include "routing/map_matcher.h"
#include "routing/routing_engine.h"
#ifdef GIS_HAS_RESULT_POLICY
#include "gis/results/geojson_output_policy.h"
#endif

#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

using json = nlohmann::json;

namespace {

struct PluginState {
    // Store owns the active generation; engine captures a shared snapshot from
    // it. Member order ensures engine is destroyed before store.
    std::unique_ptr<routing::RoadNetworkStore> store;
    std::unique_ptr<routing::RoutingEngine> engine;
#ifdef GIS_HAS_RESULT_POLICY
    std::unique_ptr<gis::results::GeoJsonOutputPolicy> output_policy;
#endif
};

// Plugin state must be destroyed by Shutdown before dlclose. A non-trivial
// global object could be destroyed by exit handlers before PluginRuntime calls
// Shutdown, which would make a second cleanup access an already-dead object.
PluginState* g_state = nullptr;

PluginTool kTools[] = {
    {"route_plan", "Plan a driving route between two WGS84 coordinates.",
     R"({"type":"object","additionalProperties":false,"properties":{"source":{"$ref":"#/$defs/coordinate"},"target":{"$ref":"#/$defs/coordinate"},"metric":{"type":"string","enum":["duration","distance"],"default":"duration"},"snapRadiusMeters":{"type":"number","minimum":1,"maximum":5000,"default":1000}},"required":["source","target"],"$defs":{"coordinate":{"type":"object","additionalProperties":false,"properties":{"longitude":{"type":"number","minimum":-180,"maximum":180},"latitude":{"type":"number","minimum":-90,"maximum":90}},"required":["longitude","latitude"]}}})"},
    {"nearest_road", "Find nearby directed road segments for a WGS84 coordinate.",
     R"({"type":"object","additionalProperties":false,"properties":{"coordinate":{"type":"object","properties":{"longitude":{"type":"number","minimum":-180,"maximum":180},"latitude":{"type":"number","minimum":-90,"maximum":90}},"required":["longitude","latitude"]},"radiusMeters":{"type":"number","minimum":1,"maximum":5000,"default":500},"limit":{"type":"integer","minimum":1,"maximum":20,"default":8}},"required":["coordinate"]})"},
    {"routing_status", "Return the active road-network version and graph size.",
     R"({"type":"object","additionalProperties":false,"properties":{}})"},
    {"map_match", "Match a WGS84 GPS trace to the directed road network using an HMM.",
     R"({"type":"object","additionalProperties":false,"properties":{"coordinates":{"type":"array","minItems":1,"maxItems":1000,"items":{"type":"object","properties":{"longitude":{"type":"number","minimum":-180,"maximum":180},"latitude":{"type":"number","minimum":-90,"maximum":90}},"required":["longitude","latitude"]}},"searchRadiusMeters":{"type":"number","minimum":1,"maximum":1000,"default":100},"maximumCandidates":{"type":"integer","minimum":1,"maximum":8,"default":4}},"required":["coordinates"]})"},
};

routing::Coordinate CoordinateFromJson(const json& value) {
    return {value.at("longitude").get<double>(), value.at("latitude").get<double>()};
}

json CoordinateJson(const routing::Coordinate& coordinate) {
    return json::array({coordinate.longitude, coordinate.latitude});
}

json GeoJsonLineString(const std::vector<routing::Coordinate>& geometry) {
    // GeoJSON uses [longitude, latitude], the reverse of Leaflet's LatLng API.
    json coordinates = json::array();
    for (const auto& point : geometry) coordinates.push_back(CoordinateJson(point));
    return {{"type", "LineString"}, {"coordinates", std::move(coordinates)}};
}

char* CopyResult(const json& value) {
    const std::string text = value.dump();
    auto* output = new char[text.size() + 1];
    std::memcpy(output, text.c_str(), text.size() + 1);
    return output;
}

json VisualizationNone() {
    return {{"kind", nullptr}, {"inline", false}, {"geojson", nullptr},
            {"resultId", nullptr}, {"mapUrl", nullptr}};
}

std::string TraceId(const json& request) {
    if (!request.contains("id")) return "mcp_notification";
    if (request["id"].is_string()) return "mcp_" + request["id"].get<std::string>();
    return "mcp_" + request["id"].dump();
}

json Envelope(std::string tool, std::string trace_id, std::string summary,
              json data, json visualization, const std::string& version) {
    return {{"schemaVersion", "1.0"}, {"tool", std::move(tool)},
            {"traceId", std::move(trace_id)}, {"summary", std::move(summary)},
            {"data", std::move(data)}, {"visualization", std::move(visualization)},
            {"meta", {{"coordinateSystem", "EPSG:4326"},
                      {"provider", "local-osm"}, {"networkVersion", version},
                      {"cacheHit", false}, {"attempts", 1},
                      {"fallbackUsed", false}}}};
}

json Success(json structured) {
    const std::string summary = structured.at("summary");
    // Preserve the bounded GIS envelope in Harness's durable text projection.
    // GeoJSON that exceeds policy limits is already external at this point.
    const std::string durable_text =
        summary + "\n\n[gis-result-envelope]\n" + structured.dump();
    return {{"isError", false},
            {"content", json::array({{{"type", "text"}, {"text", durable_text}}})},
            {"structuredContent", std::move(structured)}};
}

json Failure(const std::string& tool, const std::string& trace_id,
             const std::string& message, const std::string& version = {}) {
    auto structured = Envelope(tool, trace_id, message, json::object(),
                               VisualizationNone(), version);
    structured["error"] = {{"code", "ROUTING_ERROR"}, {"message", message},
                           {"retryable", false}, {"details", json::object()}};
    const std::string durable_text =
        message + "\n\n[gis-result-envelope]\n" + structured.dump();
    return {{"isError", true},
            {"content", json::array({{{"type", "text"}, {"text", durable_text}}})},
            {"structuredContent", std::move(structured)}};
}

#ifdef GIS_HAS_RESULT_POLICY
std::size_t EnvironmentSize(const char* name, std::size_t fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    try { return std::stoull(value); } catch (...) { return fallback; }
}

bool EnvironmentBool(const char* name, bool fallback = false) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    return std::string_view(value) == "1" || std::string_view(value) == "true" ||
           std::string_view(value) == "yes";
}

json PrepareVisualization(const json& geometry, std::size_t points,
                          json& data_geometry) {
    const auto output = g_state->output_policy->Prepare(geometry.dump(), points);
    if (!output.Ok()) throw std::runtime_error(output.error);
    if (output.value->inline_result) {
        data_geometry = geometry;
        return {{"kind", "geojson"}, {"inline", true}, {"geojson", geometry},
                {"resultId", output.value->result_id.empty() ? json(nullptr) : json(output.value->result_id)},
                {"mapUrl", output.value->map_url.empty() ? json(nullptr) : json(output.value->map_url)}};
    }
    data_geometry = nullptr;
    return {{"kind", "geojson"}, {"inline", false}, {"geojson", nullptr},
            {"resultId", output.value->result_id}, {"mapUrl", output.value->map_url}};
}
#else
json PrepareVisualization(const json& geometry, std::size_t, json& data_geometry) {
    data_geometry = geometry;
    return {{"kind", "geojson"}, {"inline", true}, {"geojson", geometry},
            {"resultId", nullptr}, {"mapUrl", nullptr}};
}
#endif

const char* GetName() { return "osm-routing-tools"; }
const char* GetVersion() { return "0.1.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }

int Initialize() {
    try {
        delete g_state;
        g_state = nullptr;
        // Data versions are deployed independently from plugin .so versions.
        // The environment variable selects which validated snapshot to stage.
        const char* file = std::getenv("MCP_ROUTING_NETWORK");
        if (file == nullptr || *file == '\0') return 0;
        const char* version_env = std::getenv("MCP_ROUTING_NETWORK_VERSION");
        const std::string version = version_env == nullptr ? "nanjing-v1" : version_env;
        auto store = std::make_unique<routing::RoadNetworkStore>();
        store->Publish(store->Stage(file, version));
        auto engine = std::make_unique<routing::RoutingEngine>(store->Current());
        g_state = new PluginState;
        g_state->store = std::move(store);
        g_state->engine = std::move(engine);
#ifdef GIS_HAS_RESULT_POLICY
        std::shared_ptr<gis::results::ResultPublisher> publisher;
        if (const char* url = std::getenv("GIS_RESULT_SERVICE_URL"); url && *url) {
            publisher = std::make_shared<gis::results::HttpResultPublisher>(url);
        }
        g_state->output_policy = std::make_unique<gis::results::GeoJsonOutputPolicy>(
            gis::results::GeoJsonOutputConfig{
                .max_inline_bytes = EnvironmentSize("GIS_MAX_INLINE_BYTES", 32U * 1024U),
                .max_inline_points = EnvironmentSize("GIS_MAX_INLINE_POINTS", 500),
                .ttl = std::chrono::seconds(EnvironmentSize("GIS_RESULT_TTL_SECONDS", 900)),
                .publish_inline_results = EnvironmentBool("GIS_PUBLISH_INLINE_RESULTS")},
            std::move(publisher));
#endif
        return 1;
    } catch (...) {
        delete g_state;
        g_state = nullptr;
        return 0;
    }
}

char* HandleRequest(const char* request_text) {
    std::string tool = "unknown";
    std::string trace_id = "mcp_unknown";
    std::string network_version;
    try {
        if (g_state == nullptr || !g_state->engine) {
            throw std::runtime_error("routing engine is not initialized");
        }
        auto& engine = *g_state->engine;
        const json request = json::parse(request_text);
        trace_id = TraceId(request);
        tool = request.at("params").at("name").get<std::string>();
        network_version = engine.Network().Version();
        const json arguments = request.at("params").value("arguments", json::object());
        // This layer contains protocol conversion only. It never reads CSR
        // arrays or implements route/search rules itself.
        if (tool == "routing_status") {
            const auto& network = engine.Network();
            return CopyResult(Success(Envelope(
                tool, trace_id, "Routing network " + network.Version() + " is ready",
                {{"networkVersion", network.Version()},
                 {"nodes", network.Graph().NodeCount()},
                 {"edges", network.Graph().EdgeCount()}},
                VisualizationNone(), network.Version())));
        }
        if (tool == "nearest_road") {
            const auto coordinate = CoordinateFromJson(arguments.at("coordinate"));
            const auto candidates = engine.NearestRoads(
                coordinate, arguments.value("radiusMeters", 500.0),
                arguments.value("limit", std::size_t{8}));
            json features = json::array();
            for (const auto& candidate : candidates) {
                features.push_back({{"type", "Feature"},
                    {"geometry", {{"type", "Point"},
                                  {"coordinates", CoordinateJson(candidate.projected)}}},
                    {"properties", {{"edge", candidate.edge},
                                    {"distanceMeters", candidate.distance_m}}}});
            }
            json geojson{{"type", "FeatureCollection"}, {"features", features}};
            json data_geometry;
            const auto visualization = PrepareVisualization(
                geojson, candidates.size(), data_geometry);
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Found " + std::to_string(candidates.size()) + " road candidates",
                {{"candidates", std::move(features)},
                 {"geometry", std::move(data_geometry)}},
                visualization, network_version)));
        }
        if (tool == "map_match") {
            std::vector<routing::Coordinate> observations;
            for (const auto& value : arguments.at("coordinates")) {
                observations.push_back(CoordinateFromJson(value));
            }
            routing::MapMatchOptions options;
            options.search_radius_m = arguments.value("searchRadiusMeters", 100.0);
            options.maximum_candidates = arguments.value("maximumCandidates", std::size_t{4});
            const routing::MapMatcher matcher{engine};
            const auto result = matcher.Match(observations, options);
            if (result.status != routing::MapMatchStatus::Ok) {
                throw std::runtime_error(
                    "map matching failed at observation " +
                    std::to_string(result.failed_observation));
            }
            json matched = json::array();
            for (const auto& point : result.observations) {
                matched.push_back({{"observed", CoordinateJson(point.observed)},
                    {"matched", CoordinateJson(point.matched)}, {"edge", point.edge},
                    {"distanceToRoadMeters", point.distance_to_road_m},
                    {"confidence", point.confidence}});
            }
            const json geometry = GeoJsonLineString(result.geometry);
            json data_geometry;
            const auto visualization = PrepareVisualization(
                geometry, result.geometry.size(), data_geometry);
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Matched " + std::to_string(result.observations.size()) +
                    " GPS observations",
                {{"logLikelihood", result.log_likelihood},
                 {"observations", std::move(matched)},
                 {"geometry", std::move(data_geometry)}},
                visualization, result.network_version)));
        }
        if (tool == "route_plan") {
            routing::CoordinateRouteRequest route;
            route.source = CoordinateFromJson(arguments.at("source"));
            route.target = CoordinateFromJson(arguments.at("target"));
            route.snap_radius_m = arguments.value("snapRadiusMeters", 1000.0);
            route.search.metric = arguments.value("metric", std::string{"duration"}) == "distance"
                                      ? routing::CostMetric::Distance
                                      : routing::CostMetric::Duration;
            const auto result = engine.Route(route);
            if (result.status != routing::RouteStatus::Ok) {
                throw std::runtime_error("no route or road candidate found");
            }
            const json geometry = GeoJsonLineString(result.geometry);
            json data_geometry;
            const auto visualization = PrepareVisualization(
                geometry, result.geometry.size(), data_geometry);
            json data{
                {"distanceMeters", result.distance_m}, {"durationSeconds", result.duration_s},
                {"sourceProjected", CoordinateJson(result.source_projected)},
                {"targetProjected", CoordinateJson(result.target_projected)},
                {"sourceSnapDistanceMeters", result.source_snap_distance_m},
                {"targetSnapDistanceMeters", result.target_snap_distance_m},
                {"geometry", std::move(data_geometry)},
                {"expandedNodes", result.statistics.expanded_nodes}};
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Route distance " + std::to_string(result.distance_m) +
                    " m, duration " + std::to_string(result.duration_s) + " s",
                std::move(data), visualization, result.network_version)));
        }
        throw std::runtime_error("unknown routing tool: " + tool);
    } catch (const std::exception& error) {
        return CopyResult(Failure(tool, trace_id, error.what(), network_version));
    }
}

void Shutdown() {
    delete g_state;
    g_state = nullptr;
}
void FreeResult(char* result) { delete[] result; }
int GetToolCount() { return static_cast<int>(std::size(kTools)); }
const PluginTool* GetTool(int index) {
    return index >= 0 && index < GetToolCount() ? &kTools[index] : nullptr;
}

PluginAPI kPlugin{MCP_PLUGIN_ABI_VERSION, sizeof(PluginAPI),
                  GetName, GetVersion, GetType, Initialize, HandleRequest, Shutdown,
                  GetToolCount, GetTool, nullptr, nullptr, nullptr, nullptr,
                  FreeResult, nullptr};

}  // namespace

extern "C" PLUGIN_API PluginAPI* CreatePlugin() { return &kPlugin; }
extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {}
