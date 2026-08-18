#include "PluginAPI.h"
#include "gis/cache/memory_response_cache.h"
#include "gis/cache/redis_response_cache.h"
#include "gis/providers/amap_provider.h"
#include "gis/providers/gcj02_coordinate_adapter.h"
#include "gis/providers/http_client.h"
#include "gis/providers/map_provider.h"
#include "gis/results/geojson_output_policy.h"
#ifdef GIS_HAS_LOCAL_OSM_PROVIDER
#include "gis/providers/local_osm_provider.h"
#endif
#include "json.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using json = nlohmann::json;

namespace {

using gis::providers::Coordinate;
using gis::providers::MapProvider;
using gis::providers::ProviderError;
using gis::providers::ProviderErrorCode;
using gis::providers::RouteProfile;

struct PluginState {
    std::unique_ptr<MapProvider> online_provider;
    std::unique_ptr<MapProvider> local_provider;
    std::string online_configuration_error;
    std::string local_configuration_error;
    std::unique_ptr<gis::results::GeoJsonOutputPolicy> output_policy;
    std::string cache_backend{"memory"};
};

PluginState* g_state = nullptr;

PluginTool kTools[] = {
    {"geocode", "Geocode a Chinese address or place name and return WGS84 candidates.",
     R"({"type":"object","additionalProperties":false,"properties":{"query":{"type":"string","minLength":1},"city":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":50,"default":10}},"required":["query"]})"},
    {"reverse_geocode", "Resolve a WGS84 coordinate to a normalized Chinese address.",
     R"({"type":"object","additionalProperties":false,"properties":{"coordinate":{"$ref":"#/$defs/coordinate"},"radiusMeters":{"type":"number","exclusiveMinimum":0,"maximum":3000,"default":1000}},"required":["coordinate"],"$defs":{"coordinate":{"type":"object","additionalProperties":false,"properties":{"longitude":{"type":"number","minimum":-180,"maximum":180},"latitude":{"type":"number","minimum":-90,"maximum":90}},"required":["longitude","latitude"]}}})"},
    {"poi_search", "Search Chinese POIs by keyword, city, and optional category.",
     R"({"type":"object","additionalProperties":false,"properties":{"keyword":{"type":"string","minLength":1},"city":{"type":"string"},"category":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":25,"default":20}},"required":["keyword"]})"},
    {"nearby_search", "Search for POIs near a WGS84 coordinate using an optional keyword or category.",
     R"({"type":"object","additionalProperties":false,"properties":{"center":{"$ref":"#/$defs/coordinate"},"radiusMeters":{"type":"number","exclusiveMinimum":0,"maximum":50000,"default":1000},"keyword":{"type":"string"},"category":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":25,"default":20}},"required":["center"],"$defs":{"coordinate":{"type":"object","additionalProperties":false,"properties":{"longitude":{"type":"number","minimum":-180,"maximum":180},"latitude":{"type":"number","minimum":-90,"maximum":90}},"required":["longitude","latitude"]}}})"},
    {"search_nearby_by_place", "Resolve a Chinese place name and search for nearby POIs in one deterministic operation. Use this for requests such as 'attractions within 3 km of Xinjiekou'.",
     R"({"type":"object","additionalProperties":false,"properties":{"place":{"type":"string","minLength":1,"description":"Place or address used as the search centre."},"city":{"type":"string","description":"Optional city constraint used to reduce place ambiguity."},"radiusMeters":{"type":"number","exclusiveMinimum":0,"maximum":50000,"default":1000},"keyword":{"type":"string","description":"POI keyword, for example attraction, restaurant, hotel, or metro station."},"category":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":25,"default":20},"candidateIndex":{"type":"integer","minimum":0,"maximum":9,"description":"Explicit geocode candidate to use after an earlier AMBIGUOUS_LOCATION result."}},"required":["place"]})"},
    {"administrative_search", "Search Chinese administrative divisions and return normalized WGS84 centres.",
     R"({"type":"object","additionalProperties":false,"properties":{"keyword":{"type":"string","minLength":1},"subdistrict":{"type":"integer","minimum":0,"maximum":3,"default":1},"limit":{"type":"integer","minimum":1,"maximum":100,"default":20}},"required":["keyword"]})"},
    {"provider_route", "Plan a walking or driving route using automatic, Amap, or local OSM routing.",
     R"({"type":"object","additionalProperties":false,"properties":{"source":{"$ref":"#/$defs/coordinate"},"target":{"$ref":"#/$defs/coordinate"},"profile":{"type":"string","enum":["walking","driving"],"default":"walking"},"provider":{"type":"string","enum":["auto","amap","local-osm"],"default":"auto"}},"required":["source","target"],"$defs":{"coordinate":{"type":"object","additionalProperties":false,"properties":{"longitude":{"type":"number","minimum":-180,"maximum":180},"latitude":{"type":"number","minimum":-90,"maximum":90}},"required":["longitude","latitude"]}}})"},
    {"map_provider_status", "Return map provider status without exposing credentials.",
     R"({"type":"object","additionalProperties":false,"properties":{}})"},
};

char* CopyResult(const json& value) {
    const std::string text = value.dump();
    auto* output = new char[text.size() + 1];
    std::memcpy(output, text.c_str(), text.size() + 1);
    return output;
}

std::string TraceId(const json& request) {
    if (!request.contains("id")) return "mcp_notification";
    if (request["id"].is_string()) return "mcp_" + request["id"].get<std::string>();
    return "mcp_" + request["id"].dump();
}

json VisualizationNone() {
    return {{"kind", nullptr}, {"inline", false}, {"geojson", nullptr},
            {"resultId", nullptr}, {"mapUrl", nullptr}};
}

std::size_t EnvironmentSize(const char* name, std::size_t default_value) {
    const char* value = std::getenv(name);
    if (!value || !*value) return default_value;
    try { return std::stoull(value); }
    catch (...) { return default_value; }
}

double EnvironmentDouble(const char* name, double default_value) {
    const char* value = std::getenv(name);
    if (!value || !*value) return default_value;
    try { return std::stod(value); }
    catch (...) { return default_value; }
}

bool EnvironmentBool(const char* name, bool fallback = false) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    return std::string_view(value) == "1" || std::string_view(value) == "true" ||
           std::string_view(value) == "yes";
}

json Visualization(const gis::results::GeoJsonOutput& output) {
    if (output.inline_result) {
        return {{"kind", "geojson"}, {"inline", true},
                {"geojson", json::parse(output.inline_geojson)},
                {"resultId", output.result_id.empty() ? json(nullptr) : json(output.result_id)},
                {"mapUrl", output.map_url.empty() ? json(nullptr) : json(output.map_url)}};
    }
    return {{"kind", "geojson"}, {"inline", false}, {"geojson", nullptr},
            {"resultId", output.result_id}, {"mapUrl", output.map_url}};
}

json Envelope(std::string tool, std::string trace_id, std::string summary,
              json data, json visualization, json meta) {
    return {{"schemaVersion", "1.0"}, {"tool", std::move(tool)},
            {"traceId", std::move(trace_id)}, {"summary", std::move(summary)},
            {"data", std::move(data)}, {"visualization", std::move(visualization)},
            {"meta", std::move(meta)}};
}

json Success(json structured) {
    const std::string summary = structured.at("summary").get<std::string>();
    // Keep a replayable copy for Harness. structuredContent is execution-local
    // there, while content is what reaches the model and transcript renderer.
    const std::string durable_text =
        summary + "\n\n[gis-result-envelope]\n" + structured.dump();
    return {{"isError", false},
            {"content", json::array({{{"type", "text"}, {"text", durable_text}}})},
            {"structuredContent", std::move(structured)}};
}

const char* ErrorCode(ProviderErrorCode code) {
    switch (code) {
        case ProviderErrorCode::InvalidArgument: return "INVALID_ARGUMENT";
        case ProviderErrorCode::NotFound: return "DATA_NOT_FOUND";
        case ProviderErrorCode::AmbiguousLocation: return "AMBIGUOUS_LOCATION";
        case ProviderErrorCode::Unauthorized: return "PROVIDER_UNAUTHORIZED";
        case ProviderErrorCode::RateLimited: return "RATE_LIMITED";
        case ProviderErrorCode::Timeout: return "TIMEOUT";
        case ProviderErrorCode::Unavailable: return "PROVIDER_UNAVAILABLE";
        case ProviderErrorCode::Unsupported: return "UNSUPPORTED_OPERATION";
        case ProviderErrorCode::Internal: return "INTERNAL_ERROR";
        case ProviderErrorCode::None: return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

json Failure(std::string tool, std::string trace_id, const ProviderError& error) {
    json structured = Envelope(tool, std::move(trace_id), error.message,
                               json::object(), VisualizationNone(), json::object());
    structured["error"] = {{"code", ErrorCode(error.code)},
                           {"message", error.message},
                           {"retryable", error.retryable},
                           {"details", json::object()}};
    const std::string durable_text =
        error.message + "\n\n[gis-result-envelope]\n" + structured.dump();
    return {{"isError", true},
            {"content", json::array({{{"type", "text"}, {"text", durable_text}}})},
            {"structuredContent", std::move(structured)}};
}

json ProviderMeta(const std::string& provider, std::size_t attempts,
                  bool fallback_used, bool cache_hit = false) {
    // Local graph coordinates are already WGS84; Amap responses are normalized
    // from GCJ-02 to WGS84 by the provider adapter before reaching this layer.
    const char* source_crs = provider == "local-osm" ? "EPSG:4326"
        : (provider == "map-services" ? "provider-dependent" : "GCJ-02");
    return {{"coordinateSystem", "EPSG:4326"}, {"provider", provider},
            {"sourceCoordinateSystem", source_crs}, {"attempts", attempts},
            {"fallbackUsed", fallback_used}, {"cacheHit", cache_hit}};
}

Coordinate CoordinateFromJson(const json& value) {
    return {.longitude = value.at("longitude").get<double>(),
            .latitude = value.at("latitude").get<double>(),
            .system = gis::providers::CoordinateSystem::Wgs84};
}

json Point(const Coordinate& coordinate) {
    return {{"type", "Point"},
            {"coordinates", json::array({coordinate.longitude, coordinate.latitude})}};
}

const char* GetName() { return "map-services-tools"; }
const char* GetVersion() { return "0.2.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }

int Initialize() {
    delete g_state;
    g_state = new PluginState;
    std::shared_ptr<gis::results::ResultPublisher> publisher;
    const char* result_service = std::getenv("GIS_RESULT_SERVICE_URL");
    if (result_service && *result_service) {
        publisher = std::make_shared<gis::results::HttpResultPublisher>(result_service);
    }
    g_state->output_policy = std::make_unique<gis::results::GeoJsonOutputPolicy>(
        gis::results::GeoJsonOutputConfig{
            .max_inline_bytes = EnvironmentSize("GIS_MAX_INLINE_BYTES", 32U * 1024U),
            .max_inline_points = EnvironmentSize("GIS_MAX_INLINE_POINTS", 500),
            .ttl = std::chrono::seconds(EnvironmentSize("GIS_RESULT_TTL_SECONDS", 900)),
            .publish_inline_results = EnvironmentBool("GIS_PUBLISH_INLINE_RESULTS")},
        std::move(publisher));
    const char* key = std::getenv("AMAP_API_KEY");
    if (key == nullptr || *key == '\0') {
        g_state->online_configuration_error =
            "AMAP_API_KEY is not configured; online map tools are unavailable";
    } else try {
        const char* base_url_env = std::getenv("AMAP_BASE_URL");
        gis::providers::AmapProviderConfig config;
        config.api_key = key;
        if (base_url_env != nullptr && *base_url_env != '\0') {
            config.base_url = base_url_env;
        }
        std::shared_ptr<gis::providers::HttpClient> http =
            std::make_shared<gis::providers::CurlHttpClient>();
        http = std::make_shared<gis::providers::RetryingHttpClient>(http,
            EnvironmentSize("GIS_PROVIDER_MAX_ATTEMPTS", 2));
        http = std::make_shared<gis::providers::RateLimitedHttpClient>(http,
            EnvironmentDouble("GIS_PROVIDER_REQUESTS_PER_SECOND", 10.0),
            EnvironmentDouble("GIS_PROVIDER_BURST", 10.0));
        std::shared_ptr<gis::cache::ResponseCache> response_cache;
        const char* cache_backend = std::getenv("GIS_CACHE_BACKEND");
        if (cache_backend && std::string_view(cache_backend) == "redis") {
            gis::cache::RedisCacheConfig redis;
            if (const char* host = std::getenv("GIS_REDIS_HOST"); host && *host) {
                redis.host = host;
            }
            redis.port = static_cast<int>(EnvironmentSize("GIS_REDIS_PORT", 6379));
            response_cache =
                std::make_shared<gis::cache::RedisResponseCache>(std::move(redis));
            g_state->cache_backend = "redis";
        } else {
            response_cache = std::make_shared<gis::cache::MemoryResponseCache>(
                EnvironmentSize("GIS_CACHE_MAX_ENTRIES", 1024));
        }
        http = std::make_shared<gis::providers::CachingHttpClient>(
            http, response_cache,
            std::chrono::seconds(EnvironmentSize("GIS_CACHE_TTL_SECONDS", 300)));
        g_state->online_provider = std::make_unique<gis::providers::AmapProvider>(
            std::move(config), std::move(http),
            std::make_shared<gis::providers::Gcj02CoordinateAdapter>());
    } catch (const std::exception& error) {
        g_state->online_configuration_error = error.what();
    }

#ifdef GIS_HAS_LOCAL_OSM_PROVIDER
    const char* network = std::getenv("MCP_ROUTING_NETWORK");
    if (network == nullptr || *network == '\0') {
        g_state->local_configuration_error =
            "MCP_ROUTING_NETWORK is not configured; local routing is unavailable";
    } else try {
        const char* version = std::getenv("MCP_ROUTING_NETWORK_VERSION");
        g_state->local_provider = std::make_unique<gis::providers::LocalOsmProvider>(
            network, version && *version ? version : "local-osm");
    } catch (const std::exception& error) {
        g_state->local_configuration_error = error.what();
    }
#else
    g_state->local_configuration_error = "Local OSM provider was not built";
#endif
    // Missing optional providers is an operational status, not a plugin load
    // failure: map_provider_status remains callable and reports each reason.
    return 1;
}

char* HandleRequest(const char* request_text) {
    std::string tool = "unknown";
    std::string trace_id = "mcp_unknown";
    try {
        const json request = json::parse(request_text);
        trace_id = TraceId(request);
        tool = request.at("params").at("name").get<std::string>();
        const json arguments = request.at("params").value("arguments", json::object());

        if (tool == "map_provider_status") {
            const bool online = g_state && g_state->online_provider;
            const bool local = g_state && g_state->local_provider;
            const bool configured = online || local;
            const std::string summary = !g_state
                ? "Map provider plugin is not initialized"
                : (configured ? "Map providers are ready"
                              : "No map provider is configured");
            return CopyResult(Success(Envelope(
                tool, trace_id, summary,
                {{"configured", configured},
                 {"providers", {
                     {"amap", {{"configured", online},
                               {"message", online ? "ready" : g_state->online_configuration_error}}},
                     {"local-osm", {{"configured", local},
                                    {"message", local ? "ready" : g_state->local_configuration_error}}}}},
                 {"cacheBackend", g_state ? g_state->cache_backend : "unavailable"}},
                VisualizationNone(), ProviderMeta("map-services", 0, false))));
        }

        if (g_state == nullptr) {
            return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable,
                 "Map provider plugin is not initialized", false}));
        }

        if (tool == "geocode") {
            if (!g_state->online_provider) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable,
                 g_state->online_configuration_error, false}));
            const auto result = g_state->online_provider->Geocode({
                .query = arguments.at("query").get<std::string>(),
                .city = arguments.value("city", std::string{}),
                .language = "zh-CN",
                .limit = arguments.value("limit", std::size_t{10}),
            });
            if (!result.Ok()) return CopyResult(Failure(tool, trace_id, result.error));
            json candidates = json::array();
            json features = json::array();
            for (const auto& candidate : result.value->candidates) {
                candidates.push_back({{"name", candidate.name},
                    {"formattedAddress", candidate.formatted_address},
                    {"longitude", candidate.coordinate.longitude},
                    {"latitude", candidate.coordinate.latitude},
                    {"providerId", candidate.provider_id}});
                features.push_back({{"type", "Feature"},
                    {"geometry", Point(candidate.coordinate)},
                    {"properties", {{"name", candidate.name},
                                    {"address", candidate.formatted_address}}}});
            }
            json geojson = {{"type", "FeatureCollection"}, {"features", features}};
            const auto output = g_state->output_policy->Prepare(
                geojson.dump(), features.size());
            if (!output.Ok()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable, output.error, true}));
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Found " + std::to_string(candidates.size()) + " geocode candidates",
                {{"candidates", candidates}},
                Visualization(*output.value),
                ProviderMeta(result.provider, result.attempts, result.fallback_used,
                             result.cache_hit))));
        }

        if (tool == "reverse_geocode") {
            if (!g_state->online_provider) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable,
                 g_state->online_configuration_error, false}));
            const auto result = g_state->online_provider->ReverseGeocode({
                .coordinate = CoordinateFromJson(arguments.at("coordinate")),
                .radius_meters = arguments.value("radiusMeters", 1000.0),
            });
            if (!result.Ok()) return CopyResult(Failure(tool, trace_id, result.error));
            const json point = Point(result.value->coordinate);
            const auto output = g_state->output_policy->Prepare(point.dump(), 1);
            if (!output.Ok()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable, output.error, true}));
            return CopyResult(Success(Envelope(
                tool, trace_id, result.value->formatted_address,
                {{"formattedAddress", result.value->formatted_address},
                 {"province", result.value->province}, {"city", result.value->city},
                 {"district", result.value->district}, {"adcode", result.value->adcode},
                 {"longitude", result.value->coordinate.longitude},
                 {"latitude", result.value->coordinate.latitude}},
                Visualization(*output.value),
                ProviderMeta(result.provider, result.attempts, result.fallback_used,
                             result.cache_hit))));
        }

        if (tool == "poi_search") {
            if (!g_state->online_provider) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable,
                 g_state->online_configuration_error, false}));
            const auto result = g_state->online_provider->SearchPoi({
                .keyword = arguments.at("keyword").get<std::string>(),
                .city = arguments.value("city", std::string{}),
                .category = arguments.value("category", std::string{}),
                .limit = arguments.value("limit", std::size_t{20}),
            });
            if (!result.Ok()) return CopyResult(Failure(tool, trace_id, result.error));
            json pois = json::array();
            json features = json::array();
            for (const auto& poi : result.value->pois) {
                pois.push_back({{"id", poi.id}, {"name", poi.name},
                    {"category", poi.category}, {"address", poi.address},
                    {"longitude", poi.coordinate.longitude},
                    {"latitude", poi.coordinate.latitude}});
                features.push_back({{"type", "Feature"}, {"geometry", Point(poi.coordinate)},
                    {"properties", {{"id", poi.id}, {"name", poi.name},
                                    {"category", poi.category}}}});
            }
            const json geojson = {{"type", "FeatureCollection"}, {"features", features}};
            const auto output = g_state->output_policy->Prepare(
                geojson.dump(), features.size());
            if (!output.Ok()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable, output.error, true}));
            return CopyResult(Success(Envelope(
                tool, trace_id, "Found " + std::to_string(pois.size()) + " POIs",
                {{"pois", std::move(pois)}}, Visualization(*output.value),
                ProviderMeta(result.provider, result.attempts, result.fallback_used,
                             result.cache_hit))));
        }

        if (tool == "nearby_search") {
            if (!g_state->online_provider) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable,
                 g_state->online_configuration_error, false}));
            const auto result = g_state->online_provider->SearchNearby({
                .center = CoordinateFromJson(arguments.at("center")),
                .radius_meters = arguments.value("radiusMeters", 1000.0),
                .keyword = arguments.value("keyword", std::string{}),
                .category = arguments.value("category", std::string{}),
                .limit = arguments.value("limit", std::size_t{20}),
            });
            if (!result.Ok()) return CopyResult(Failure(tool, trace_id, result.error));
            json pois = json::array();
            json features = json::array();
            for (const auto& poi : result.value->pois) {
                pois.push_back({{"id", poi.id}, {"name", poi.name},
                    {"category", poi.category}, {"address", poi.address},
                    {"longitude", poi.coordinate.longitude},
                    {"latitude", poi.coordinate.latitude},
                    {"distanceMeters", poi.distance_meters}});
                features.push_back({{"type", "Feature"}, {"geometry", Point(poi.coordinate)},
                    {"properties", {{"id", poi.id}, {"name", poi.name},
                                    {"distanceMeters", poi.distance_meters}}}});
            }
            json geojson = {{"type", "FeatureCollection"}, {"features", features}};
            const auto output = g_state->output_policy->Prepare(
                geojson.dump(), features.size());
            if (!output.Ok()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable, output.error, true}));
            return CopyResult(Success(Envelope(
                tool, trace_id, "Found " + std::to_string(pois.size()) + " nearby POIs",
                {{"pois", pois}},
                Visualization(*output.value),
                ProviderMeta(result.provider, result.attempts, result.fallback_used,
                             result.cache_hit))));
        }

        if (tool == "search_nearby_by_place") {
            if (!g_state->online_provider) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable,
                 g_state->online_configuration_error, false}));

            // Keep this common two-provider-call chain inside one Tool. The
            // Agent supplies business intent; the plugin owns deterministic
            // sequencing, coordinate handoff, and ambiguity protection.
            const auto geocode = g_state->online_provider->Geocode({
                .query = arguments.at("place").get<std::string>(),
                .city = arguments.value("city", std::string{}),
                .language = "zh-CN",
                .limit = std::size_t{10},
            });
            if (!geocode.Ok()) return CopyResult(Failure(tool, trace_id, geocode.error));
            if (geocode.value->candidates.empty()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::NotFound, "No geocode candidate found for place", false}));

            const bool explicit_candidate = arguments.contains("candidateIndex");
            const std::size_t candidate_index =
                arguments.value("candidateIndex", std::size_t{0});
            if (candidate_index >= geocode.value->candidates.size()) {
                return CopyResult(Failure(tool, trace_id,
                    {ProviderErrorCode::InvalidArgument,
                     "candidateIndex is outside the returned geocode candidates", false}));
            }
            if (geocode.value->candidates.size() > 1 && !explicit_candidate) {
                json candidates = json::array();
                for (std::size_t index = 0; index < geocode.value->candidates.size(); ++index) {
                    const auto& candidate = geocode.value->candidates[index];
                    candidates.push_back({{"candidateIndex", index}, {"name", candidate.name},
                        {"formattedAddress", candidate.formatted_address},
                        {"longitude", candidate.coordinate.longitude},
                        {"latitude", candidate.coordinate.latitude}});
                }
                json structured = Envelope(tool, trace_id,
                    "Place is ambiguous; choose a geocode candidate before searching",
                    {{"candidates", std::move(candidates)}}, VisualizationNone(),
                    ProviderMeta(geocode.provider, geocode.attempts,
                                 geocode.fallback_used, geocode.cache_hit));
                structured["error"] = {{"code", "AMBIGUOUS_LOCATION"},
                    {"message", "Place is ambiguous; retry with candidateIndex"},
                    {"retryable", false}, {"details", json::object()}};
                const std::string durable_text = structured.at("summary").get<std::string>() +
                    "\n\n[gis-result-envelope]\n" + structured.dump();
                return CopyResult({{"isError", true},
                    {"content", json::array({{{"type", "text"}, {"text", durable_text}}})},
                    {"structuredContent", std::move(structured)}});
            }

            const auto& selected = geocode.value->candidates[candidate_index];
            const auto nearby = g_state->online_provider->SearchNearby({
                .center = selected.coordinate,
                .radius_meters = arguments.value("radiusMeters", 1000.0),
                .keyword = arguments.value("keyword", std::string{}),
                .category = arguments.value("category", std::string{}),
                .limit = arguments.value("limit", std::size_t{20}),
            });
            if (!nearby.Ok()) return CopyResult(Failure(tool, trace_id, nearby.error));

            json pois = json::array();
            json features = json::array({{{"type", "Feature"},
                {"geometry", Point(selected.coordinate)},
                {"properties", {{"role", "searchCenter"}, {"name", selected.name}}}}});
            for (const auto& poi : nearby.value->pois) {
                pois.push_back({{"id", poi.id}, {"name", poi.name},
                    {"category", poi.category}, {"address", poi.address},
                    {"longitude", poi.coordinate.longitude},
                    {"latitude", poi.coordinate.latitude},
                    {"distanceMeters", poi.distance_meters}});
                features.push_back({{"type", "Feature"}, {"geometry", Point(poi.coordinate)},
                    {"properties", {{"role", "poi"}, {"id", poi.id},
                                    {"name", poi.name},
                                    {"distanceMeters", poi.distance_meters}}}});
            }
            const json geojson = {{"type", "FeatureCollection"}, {"features", features}};
            const auto output = g_state->output_policy->Prepare(
                geojson.dump(), features.size());
            if (!output.Ok()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable, output.error, true}));
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Resolved " + selected.name + " and found " +
                    std::to_string(pois.size()) + " nearby POIs",
                {{"resolvedPlace", {{"candidateIndex", candidate_index},
                    {"name", selected.name},
                    {"formattedAddress", selected.formatted_address},
                    {"longitude", selected.coordinate.longitude},
                    {"latitude", selected.coordinate.latitude}}},
                 {"radiusMeters", arguments.value("radiusMeters", 1000.0)},
                 {"pois", std::move(pois)}},
                Visualization(*output.value),
                {{"coordinateSystem", "EPSG:4326"},
                 {"provider", nearby.provider},
                 {"sourceCoordinateSystem", "GCJ-02"},
                 {"attempts", geocode.attempts + nearby.attempts},
                 {"providerOperations", 2},
                 {"fallbackUsed", geocode.fallback_used || nearby.fallback_used},
                 {"cacheHit", geocode.cache_hit && nearby.cache_hit}})));
        }

        if (tool == "administrative_search") {
            if (!g_state->online_provider) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable,
                 g_state->online_configuration_error, false}));
            const auto result = g_state->online_provider->SearchAdministrative({
                .keyword = arguments.at("keyword").get<std::string>(),
                .subdistrict = arguments.value("subdistrict", 1),
                .limit = arguments.value("limit", std::size_t{20}),
            });
            if (!result.Ok()) return CopyResult(Failure(tool, trace_id, result.error));
            json areas = json::array();
            json features = json::array();
            for (const auto& area : result.value->areas) {
                areas.push_back({{"name", area.name}, {"adcode", area.adcode},
                                 {"level", area.level},
                                 {"longitude", area.center.longitude},
                                 {"latitude", area.center.latitude}});
                features.push_back({{"type", "Feature"}, {"geometry", Point(area.center)},
                    {"properties", {{"name", area.name}, {"adcode", area.adcode},
                                    {"level", area.level}}}});
            }
            const json geojson = {{"type", "FeatureCollection"}, {"features", features}};
            const auto output = g_state->output_policy->Prepare(
                geojson.dump(), features.size());
            if (!output.Ok()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable, output.error, true}));
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Found " + std::to_string(areas.size()) + " administrative area(s)",
                {{"areas", std::move(areas)}}, Visualization(*output.value),
                ProviderMeta(result.provider, result.attempts, result.fallback_used,
                             result.cache_hit))));
        }

        if (tool == "provider_route") {
            const RouteProfile profile =
                arguments.value("profile", std::string{"walking"}) == "driving"
                    ? RouteProfile::Driving : RouteProfile::Walking;
            const std::string requested = arguments.value("provider", std::string{"auto"});
            MapProvider* selected = nullptr;
            if (requested == "amap") selected = g_state->online_provider.get();
            if (requested == "local-osm") selected = g_state->local_provider.get();
            if (requested == "auto") {
                selected = profile == RouteProfile::Driving && g_state->local_provider
                    ? g_state->local_provider.get() : g_state->online_provider.get();
            }
            if (!selected) {
                const std::string message = requested == "local-osm"
                    ? g_state->local_configuration_error
                    : g_state->online_configuration_error;
                return CopyResult(Failure(tool, trace_id,
                    {ProviderErrorCode::Unavailable, message, false}));
            }
            const gis::providers::RouteRequest route_request{
                .source = CoordinateFromJson(arguments.at("source")),
                .target = CoordinateFromJson(arguments.at("target")),
                .profile = profile,
            };
            auto result = selected->PlanRoute(route_request);
            // Auto mode may cross a provider boundary only for technical or
            // capability failures. A genuine local NotFound remains meaningful.
            if (!result.Ok() && requested == "auto" &&
                selected == g_state->local_provider.get() &&
                g_state->online_provider &&
                (result.error.retryable || result.error.code == ProviderErrorCode::Unsupported)) {
                result = g_state->online_provider->PlanRoute(route_request);
                result.fallback_used = true;
                ++result.attempts;
            }
            if (!result.Ok()) return CopyResult(Failure(tool, trace_id, result.error));
            const json geometry = json::parse(result.value->geometry_geojson);
            const auto point_count = geometry.value("coordinates", json::array()).size();
            const auto output = g_state->output_policy->Prepare(
                geometry.dump(), point_count);
            if (!output.Ok()) return CopyResult(Failure(tool, trace_id,
                {ProviderErrorCode::Unavailable, output.error, true}));
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Route distance " + std::to_string(result.value->distance_meters) +
                    " metres, duration " + std::to_string(result.value->duration_seconds) +
                    " seconds",
                {{"distanceMeters", result.value->distance_meters},
                 {"durationSeconds", result.value->duration_seconds},
                 {"profile", profile == RouteProfile::Walking ? "walking" : "driving"},
                 {"geometry", output.value->inline_result ? geometry : json(nullptr)}},
                Visualization(*output.value),
                ProviderMeta(result.provider, result.attempts, result.fallback_used,
                             result.cache_hit))));
        }

        return CopyResult(Failure(tool, trace_id,
            {ProviderErrorCode::Unsupported, "Unknown map service tool", false}));
    } catch (const json::exception& error) {
        return CopyResult(Failure(tool, trace_id,
            {ProviderErrorCode::InvalidArgument, error.what(), false}));
    } catch (const std::exception& error) {
        return CopyResult(Failure(tool, trace_id,
            {ProviderErrorCode::Internal, error.what(), false}));
    }
}

void Shutdown() { delete g_state; g_state = nullptr; }
void FreeResult(char* result) { delete[] result; }
int GetToolCount() { return static_cast<int>(std::size(kTools)); }
const PluginTool* GetTool(int index) {
    return index >= 0 && index < GetToolCount() ? &kTools[index] : nullptr;
}

PluginAPI kPlugin{MCP_PLUGIN_ABI_VERSION, sizeof(PluginAPI), GetName, GetVersion,
    GetType, Initialize, HandleRequest, Shutdown, GetToolCount, GetTool,
    nullptr, nullptr, nullptr, nullptr, FreeResult, nullptr};

}  // namespace

extern "C" PLUGIN_API PluginAPI* CreatePlugin() { return &kPlugin; }
extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {}
