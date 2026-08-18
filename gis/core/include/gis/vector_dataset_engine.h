#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gis {

struct DatasetFieldInfo {
    std::string name;
    std::string type;
    bool nullable{true};
    int width{0};
    int precision{0};
};

struct DatasetExtent {
    double min_x{};
    double min_y{};
    double max_x{};
    double max_y{};
};

struct DatasetLayerInfo {
    std::string name;
    std::string geometry_type;
    std::string crs;
    long long feature_count{-1};
    std::optional<DatasetExtent> extent;
    std::vector<DatasetFieldInfo> fields;
};

struct DatasetInfoResult {
    bool success{false};
    std::string driver;
    std::vector<DatasetLayerInfo> layers;
    std::string error;
};

struct FeaturesWithinResult {
    bool success{false};
    std::string geojson;
    std::string layer_name;
    std::string crs;
    std::size_t feature_count{0};
    bool truncated{false};
    std::string error;
};

// Read-only facade around GDAL's vector dataset API. It keeps GDAL ownership
// details out of plugins and returns plain C++ values that are safe across the
// plugin boundary.
class VectorDatasetEngine {
public:
    [[nodiscard]] DatasetInfoResult Inspect(std::string_view path) const;

    [[nodiscard]] FeaturesWithinResult FeaturesWithin(
        std::string_view path, std::string_view polygon_geojson,
        std::string_view layer_name, std::size_t limit) const;
};

}  // namespace gis
