#include "PluginAPI.h"
#include "gis/coordinate_transformer.h"
#include "gis/gdal_runtime.h"
#include "gis/geometry_engine.h"
#include "gis/vector_dataset_engine.h"
#include "gis/results/geojson_output_policy.h"
#include "json.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using json = nlohmann::json;

namespace {

std::unique_ptr<gis::results::GeoJsonOutputPolicy> g_output_policy;
std::optional<std::filesystem::path> g_dataset_root;

PluginTool kTools[] = {
    {
        "geometry_validate",
        "Validate a GeoJSON geometry, including topology checks when supported.",
        R"({"type":"object","additionalProperties":false,"properties":{"geometry":{"type":"object","description":"A GeoJSON Geometry object."}},"required":["geometry"]})",
    },
    {
        "coordinate_transform",
        "Transform one coordinate between standard CRS definitions such as EPSG:4326 and EPSG:3857.",
        R"({"type":"object","additionalProperties":false,"properties":{"coordinate":{"type":"object","additionalProperties":false,"properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number","default":0}},"required":["x","y"]},"sourceCrs":{"type":"string","minLength":1,"default":"EPSG:4326"},"targetCrs":{"type":"string","minLength":1}},"required":["coordinate","targetCrs"]})",
    },
    {
        "point_in_polygon",
        "Test whether a GeoJSON Polygon or MultiPolygon contains a GeoJSON Point.",
        R"({"type":"object","additionalProperties":false,"properties":{"polygon":{"type":"object"},"point":{"type":"object"}},"required":["polygon","point"]})",
    },
    {
        "geometry_distance",
        "Measure the shortest distance in metres between two GeoJSON geometries.",
        R"({"type":"object","additionalProperties":false,"properties":{"first":{"type":"object"},"second":{"type":"object"},"sourceCrs":{"type":"string","default":"EPSG:4326"}},"required":["first","second"]})",
    },
    {
        "geometry_area",
        "Measure the area of a GeoJSON Polygon or MultiPolygon in square metres.",
        R"({"type":"object","additionalProperties":false,"properties":{"geometry":{"type":"object"},"sourceCrs":{"type":"string","default":"EPSG:4326"}},"required":["geometry"]})",
    },
    {
        "geometry_buffer",
        "Create a metre-based buffer around a GeoJSON geometry and return it in the source CRS.",
        R"({"type":"object","additionalProperties":false,"properties":{"geometry":{"type":"object"},"distanceMeters":{"type":"number","minimum":0,"maximum":100000},"sourceCrs":{"type":"string","default":"EPSG:4326"},"quadrantSegments":{"type":"integer","minimum":1,"maximum":64,"default":8}},"required":["geometry","distanceMeters"]})",
    },
    {
        "dataset_info",
        "Inspect metadata for a local vector dataset under the configured GIS dataset root.",
        R"({"type":"object","additionalProperties":false,"properties":{"path":{"type":"string","minLength":1,"description":"Path relative to GIS_DATASET_ROOT."}},"required":["path"]})",
    },
    {
        "features_within",
        "Return vector features intersecting a Polygon or MultiPolygon from a dataset under the configured GIS dataset root.",
        R"({"type":"object","additionalProperties":false,"properties":{"path":{"type":"string","minLength":1,"description":"Path relative to GIS_DATASET_ROOT."},"layer":{"type":"string","default":""},"polygon":{"type":"object","description":"GeoJSON Polygon or MultiPolygon in the dataset CRS."},"limit":{"type":"integer","minimum":1,"maximum":1000,"default":100}},"required":["path","polygon"]})",
    },
};

char* CopyResult(const json& value) {
    const std::string text = value.dump();
    auto* output = new char[text.size() + 1];
    std::memcpy(output, text.c_str(), text.size() + 1);
    return output;
}

json VisualizationNone() {
    return {
        {"kind", nullptr},
        {"inline", false},
        {"geojson", nullptr},
        {"resultId", nullptr},
        {"mapUrl", nullptr},
    };
}

// All GIS tools start with the same versioned envelope. Keeping this shape
// stable lets the future Harness renderer consume different tools without a
// separate response adapter for every plugin function.
json Envelope(std::string tool, std::string trace_id, std::string summary, json data,
              json visualization, json meta) {
    return {
        {"schemaVersion", "1.0"},
        {"tool", std::move(tool)},
        {"traceId", std::move(trace_id)},
        {"summary", std::move(summary)},
        {"data", std::move(data)},
        {"visualization", std::move(visualization)},
        {"meta", std::move(meta)},
    };
}

json Success(json structured) {
    const std::string summary = structured.at("summary").get<std::string>();
    // Harness persists the MCP text projection but deliberately drops the
    // execution-local structured value. Append the same bounded envelope so
    // later Agent steps and replay-stable GIS renderers can recover it. Large
    // GeoJSON has already been replaced with resultId + mapUrl by the output
    // policy, so this never defeats the context-size guard.
    const std::string durable_text =
        summary + "\n\n[gis-result-envelope]\n" + structured.dump();
    return {
        {"isError", false},
        {"content", json::array({{{"type", "text"}, {"text", durable_text}}})},
        {"structuredContent", std::move(structured)},
    };
}

json Failure(std::string tool, std::string trace_id, std::string code,
             std::string message, bool retryable = false) {
    json structured = Envelope(
        std::move(tool), std::move(trace_id), message, json::object(), VisualizationNone(),
        json::object());
    structured["error"] = {
        {"code", std::move(code)},
        {"message", message},
        {"retryable", retryable},
        {"details", json::object()},
    };
    const std::string durable_text =
        message + "\n\n[gis-result-envelope]\n" + structured.dump();
    return {
        {"isError", true},
        {"content", json::array({{{"type", "text"}, {"text", durable_text}}})},
        {"structuredContent", std::move(structured)},
    };
}

std::string TraceId(const json& request) {
    if (!request.contains("id")) return "mcp_notification";
    if (request["id"].is_string()) return "mcp_" + request["id"].get<std::string>();
    return "mcp_" + request["id"].dump();
}

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

std::optional<std::filesystem::path> ResolveDatasetPath(
    std::string_view relative_path, std::string& error) {
    if (!g_dataset_root) {
        error = "Local dataset access is disabled; configure GIS_DATASET_ROOT";
        return std::nullopt;
    }
    const std::filesystem::path requested(relative_path);
    if (requested.empty() || requested.is_absolute()) {
        error = "Dataset path must be relative to GIS_DATASET_ROOT";
        return std::nullopt;
    }

    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(*g_dataset_root / requested, ec);
    if (ec || !std::filesystem::is_regular_file(resolved, ec)) {
        error = "Dataset path does not identify a readable file";
        return std::nullopt;
    }
    // Compare path components, not string prefixes: /data2 must not pass a
    // root check for /data.
    auto root_part = g_dataset_root->begin();
    auto path_part = resolved.begin();
    for (; root_part != g_dataset_root->end(); ++root_part, ++path_part) {
        if (path_part == resolved.end() || *root_part != *path_part) {
            error = "Dataset path escapes GIS_DATASET_ROOT";
            return std::nullopt;
        }
    }
    return resolved;
}

json DatasetLayerJson(const gis::DatasetLayerInfo& layer) {
    json fields = json::array();
    for (const auto& field : layer.fields) {
        fields.push_back({{"name", field.name}, {"type", field.type},
                          {"nullable", field.nullable}, {"width", field.width},
                          {"precision", field.precision}});
    }
    json extent = nullptr;
    if (layer.extent) {
        extent = {{"minX", layer.extent->min_x}, {"minY", layer.extent->min_y},
                  {"maxX", layer.extent->max_x}, {"maxY", layer.extent->max_y}};
    }
    return {{"name", layer.name}, {"geometryType", layer.geometry_type},
            {"crs", layer.crs.empty() ? json(nullptr) : json(layer.crs)},
            {"featureCount", layer.feature_count}, {"extent", std::move(extent)},
            {"fields", std::move(fields)}};
}

std::size_t CoordinatePointCount(const json& value) {
    if (!value.is_array()) return 0;
    if (value.size() >= 2 && value[0].is_number() && value[1].is_number()) return 1;
    std::size_t count = 0;
    for (const auto& child : value) count += CoordinatePointCount(child);
    return count;
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

const char* GetName() { return "gis-analysis-tools"; }
const char* GetVersion() { return "0.1.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }

int Initialize() {
    try {
        gis::GdalRuntime::EnsureInitialized();
        std::shared_ptr<gis::results::ResultPublisher> publisher;
        const char* url = std::getenv("GIS_RESULT_SERVICE_URL");
        if (url && *url) {
            publisher = std::make_shared<gis::results::HttpResultPublisher>(url);
        }
        g_output_policy = std::make_unique<gis::results::GeoJsonOutputPolicy>(
            gis::results::GeoJsonOutputConfig{
                .max_inline_bytes = EnvironmentSize("GIS_MAX_INLINE_BYTES", 32U * 1024U),
                .max_inline_points = EnvironmentSize("GIS_MAX_INLINE_POINTS", 500),
                .ttl = std::chrono::seconds(EnvironmentSize("GIS_RESULT_TTL_SECONDS", 900)),
                .publish_inline_results = EnvironmentBool("GIS_PUBLISH_INLINE_RESULTS")},
            std::move(publisher));
        g_dataset_root.reset();
        const char* dataset_root = std::getenv("GIS_DATASET_ROOT");
        if (dataset_root != nullptr && *dataset_root != '\0') {
            std::error_code ec;
            auto canonical = std::filesystem::weakly_canonical(dataset_root, ec);
            if (ec || !std::filesystem::is_directory(canonical, ec)) return 0;
            g_dataset_root = std::move(canonical);
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

char* HandleRequest(const char* request_text) {
    std::string tool = "unknown";
    std::string trace_id = "mcp_unknown";
    try {
        if (request_text == nullptr) {
            return CopyResult(Failure(tool, trace_id, "INVALID_ARGUMENT", "Request must not be null"));
        }

        const json request = json::parse(request_text);
        trace_id = TraceId(request);
        tool = request.at("params").at("name").get<std::string>();
        const json arguments = request.at("params").value("arguments", json::object());

        if (tool == "geometry_validate") {
            const gis::GeometryEngine engine;
            const auto result = engine.ValidateGeoJson(arguments.at("geometry").dump());
            if (!result.valid) {
                return CopyResult(Failure(tool, trace_id, "INVALID_GEOMETRY", result.error));
            }

            return CopyResult(Success(Envelope(
                tool, trace_id, "GeoJSON geometry is valid",
                {{"valid", true}, {"geometryType", result.geometry_type}},
                VisualizationNone(),
                {{"coordinateSystem", nullptr}, {"provider", "local-gdal"}})));
        }

        if (tool == "coordinate_transform") {
            const auto& input = arguments.at("coordinate");
            const std::string source_crs =
                arguments.value("sourceCrs", std::string{"EPSG:4326"});
            const std::string target_crs = arguments.at("targetCrs").get<std::string>();
            const gis::Coordinate coordinate{
                .x = input.at("x").get<double>(),
                .y = input.at("y").get<double>(),
                .z = input.value("z", 0.0),
            };

            const gis::CoordinateTransformer transformer;
            const auto result = transformer.Transform(
                coordinate, source_crs, target_crs);
            if (!result.success) {
                return CopyResult(Failure(tool, trace_id, "UNSUPPORTED_CRS", result.error));
            }

            json point = {
                {"type", "Point"},
                {"coordinates", json::array(
                    {result.coordinate.x, result.coordinate.y})},
            };
            json visualization = {
                {"kind", "geojson"},
                {"inline", true},
                {"geojson", point},
                {"resultId", nullptr},
                {"mapUrl", nullptr},
            };
            return CopyResult(Success(Envelope(
                tool, trace_id, "Coordinate transformed from " + source_crs + " to " + target_crs,
                {{"coordinate", {{"x", result.coordinate.x},
                                  {"y", result.coordinate.y},
                                  {"z", result.coordinate.z}}},
                 {"sourceCrs", source_crs},
                 {"targetCrs", target_crs}},
                std::move(visualization),
                {{"coordinateSystem", target_crs}, {"provider", "local-gdal"}})));
        }

        if (tool == "point_in_polygon") {
            const gis::GeometryEngine engine;
            const auto result = engine.Contains(
                arguments.at("polygon").dump(), arguments.at("point").dump());
            if (!result.success) {
                return CopyResult(Failure(tool, trace_id, "INVALID_GEOMETRY", result.error));
            }
            const std::string summary = result.value
                ? "The polygon contains the point"
                : "The polygon does not contain the point";
            return CopyResult(Success(Envelope(
                tool, trace_id, summary, {{"contains", result.value}}, VisualizationNone(),
                {{"coordinateSystem", nullptr}, {"provider", "local-gdal"}})));
        }

        if (tool == "geometry_distance") {
            const std::string source_crs =
                arguments.value("sourceCrs", std::string{"EPSG:4326"});
            const gis::GeometryEngine engine;
            const auto result = engine.DistanceMeters(
                arguments.at("first").dump(), arguments.at("second").dump(),
                source_crs);
            if (!result.success) {
                return CopyResult(Failure(tool, trace_id, "INVALID_GEOMETRY", result.error));
            }
            return CopyResult(Success(Envelope(
                tool, trace_id, "Geometry distance is " + std::to_string(result.value) + " metres",
                {{"distanceMeters", result.value}}, VisualizationNone(),
                {{"coordinateSystem", source_crs}, {"provider", "local-gdal"}})));
        }

        if (tool == "geometry_area") {
            const std::string source_crs =
                arguments.value("sourceCrs", std::string{"EPSG:4326"});
            const gis::GeometryEngine engine;
            const auto result = engine.AreaSquareMeters(
                arguments.at("geometry").dump(), source_crs);
            if (!result.success) {
                return CopyResult(Failure(tool, trace_id, "INVALID_GEOMETRY", result.error));
            }
            return CopyResult(Success(Envelope(
                tool, trace_id, "Geometry area is " + std::to_string(result.value) + " square metres",
                {{"areaSquareMeters", result.value}}, VisualizationNone(),
                {{"coordinateSystem", source_crs}, {"provider", "local-gdal"}})));
        }

        if (tool == "geometry_buffer") {
            // MCP clients should honour inputSchema, but model-generated Tool
            // calls can still arrive incomplete. Validate before using at() so
            // the Agent receives an actionable contract error rather than an
            // implementation-specific nlohmann::json exception.
            json missing = json::array();
            if (!arguments.contains("geometry")) missing.push_back("geometry");
            if (!arguments.contains("distanceMeters")) {
                missing.push_back("distanceMeters");
            }
            if (!missing.empty()) {
                return CopyResult(Failure(
                    tool, trace_id, "MISSING_REQUIRED_PARAMETER",
                    "geometry_buffer requires parameters: " + missing.dump() +
                        ". Retry with the GeoJSON geometry from the previous Tool "
                        "result and the requested buffer distance in metres."));
            }
            const std::string source_crs =
                arguments.value("sourceCrs", std::string{"EPSG:4326"});
            const double distance_meters = arguments.at("distanceMeters").get<double>();
            const int quadrant_segments = arguments.value("quadrantSegments", 8);
            const gis::GeometryEngine engine;
            const auto result = engine.BufferMeters(
                arguments.at("geometry").dump(), source_crs, distance_meters,
                quadrant_segments);
            if (!result.success) {
                return CopyResult(Failure(tool, trace_id, "INVALID_GEOMETRY", result.error));
            }
            json geometry = json::parse(result.geojson);
            const auto output = g_output_policy->Prepare(
                result.geojson, CoordinatePointCount(geometry.at("coordinates")));
            if (!output.Ok()) {
                return CopyResult(Failure(tool, trace_id, "RESULT_STORE_UNAVAILABLE",
                                          output.error, true));
            }
            return CopyResult(Success(Envelope(
                tool, trace_id, "Created a " + std::to_string(distance_meters) + " metre buffer",
                {{"distanceMeters", distance_meters},
                 {"geometry", output.value->inline_result ? geometry : json(nullptr)}},
                Visualization(*output.value),
                {{"coordinateSystem", source_crs}, {"provider", "local-gdal"}})));
        }

        if (tool == "dataset_info") {
            // Keep an owned copy for both sandbox resolution and the response.
            // Publishing a visualization can perform an HTTP round trip; the
            // response must not depend on re-reading request-owned JSON after it.
            const std::string relative_path =
                arguments.at("path").get<std::string>();
            std::string path_error;
            const auto path = ResolveDatasetPath(relative_path, path_error);
            if (!path) {
                return CopyResult(Failure(tool, trace_id, "DATASET_ACCESS_DENIED",
                                          path_error));
            }
            const gis::VectorDatasetEngine engine;
            const auto result = engine.Inspect(path->string());
            if (!result.success) {
                return CopyResult(Failure(tool, trace_id, "DATASET_OPEN_FAILED",
                                          result.error));
            }
            json layers = json::array();
            for (const auto& layer : result.layers) {
                layers.push_back(DatasetLayerJson(layer));
            }
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Inspected " + std::to_string(result.layers.size()) + " dataset layer(s)",
                {{"path", relative_path}, {"driver", result.driver},
                 {"layers", std::move(layers)}},
                VisualizationNone(),
                {{"coordinateSystem", nullptr}, {"provider", "local-gdal"}})));
        }

        if (tool == "features_within") {
            const std::string relative_path =
                arguments.at("path").get<std::string>();
            std::string path_error;
            const auto path = ResolveDatasetPath(relative_path, path_error);
            if (!path) {
                return CopyResult(Failure(tool, trace_id, "DATASET_ACCESS_DENIED",
                                          path_error));
            }
            const std::size_t limit = arguments.value("limit", std::size_t{100});
            if (limit == 0 || limit > 1000) {
                return CopyResult(Failure(tool, trace_id, "INVALID_ARGUMENT",
                                          "limit must be between 1 and 1000"));
            }
            const gis::VectorDatasetEngine engine;
            const auto result = engine.FeaturesWithin(
                path->string(), arguments.at("polygon").dump(),
                arguments.value("layer", std::string{}), limit);
            if (!result.success) {
                return CopyResult(Failure(tool, trace_id, "DATASET_QUERY_FAILED",
                                          result.error));
            }
            json collection = json::parse(result.geojson);
            const auto output = g_output_policy->Prepare(
                result.geojson, result.feature_count);
            if (!output.Ok()) {
                return CopyResult(Failure(tool, trace_id, "RESULT_STORE_UNAVAILABLE",
                                          output.error, true));
            }
            return CopyResult(Success(Envelope(
                tool, trace_id,
                "Found " + std::to_string(result.feature_count) + " feature(s)",
                {{"path", relative_path}, {"layer", result.layer_name},
                 {"featureCount", result.feature_count}, {"truncated", result.truncated},
                 {"featureCollection",
                  output.value->inline_result ? collection : json(nullptr)}},
                Visualization(*output.value),
                {{"coordinateSystem", result.crs.empty() ? json(nullptr) : json(result.crs)},
                 {"provider", "local-gdal"}})));
        }

        return CopyResult(Failure(tool, trace_id, "TOOL_NOT_FOUND", "Unknown GIS analysis tool"));
    } catch (const json::exception& error) {
        return CopyResult(Failure(tool, trace_id, "INVALID_ARGUMENT", error.what()));
    } catch (const std::exception& error) {
        return CopyResult(Failure(tool, trace_id, "INTERNAL_ERROR", error.what()));
    } catch (...) {
        return CopyResult(Failure(tool, trace_id, "INTERNAL_ERROR", "Unknown GIS analysis failure"));
    }
}

void Shutdown() {
    g_dataset_root.reset();
    g_output_policy.reset();
}
void FreeResult(char* result) { delete[] result; }

int GetToolCount() { return static_cast<int>(std::size(kTools)); }
const PluginTool* GetTool(int index) {
    return index >= 0 && index < GetToolCount() ? &kTools[index] : nullptr;
}

PluginAPI kPlugin{
    MCP_PLUGIN_ABI_VERSION,
    sizeof(PluginAPI),
    GetName,
    GetVersion,
    GetType,
    Initialize,
    HandleRequest,
    Shutdown,
    GetToolCount,
    GetTool,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    FreeResult,
    nullptr,
};

}  // namespace

extern "C" PLUGIN_API PluginAPI* CreatePlugin() { return &kPlugin; }
extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {}
