#pragma once

#include <string>
#include <string_view>

namespace gis {

struct GeometryValidationResult {
    bool valid{false};
    std::string geometry_type;
    std::string error;
};

struct SpatialPredicateResult {
    bool success{false};
    bool value{false};
    std::string error;
};

struct GeometryMeasurementResult {
    bool success{false};
    double value{0.0};
    std::string error;
};

struct GeometryOperationResult {
    bool success{false};
    std::string geojson;
    std::string error;
};

// Project-owned facade around OGR geometry handling. OGR pointers deliberately
// remain inside the implementation and therefore never cross the plugin ABI.
class GeometryEngine final {
public:
    [[nodiscard]] GeometryValidationResult ValidateGeoJson(
        std::string_view geojson) const;

    // Both geometries must use the same coordinate space. Topological
    // predicates do not require a CRS because they do not measure length.
    [[nodiscard]] SpatialPredicateResult Contains(
        std::string_view container_geojson,
        std::string_view candidate_geojson) const;

    // Metric operations transform input into a local projected CRS before
    // measuring. This avoids treating degrees as metres for EPSG:4326 data.
    [[nodiscard]] GeometryMeasurementResult DistanceMeters(
        std::string_view first_geojson,
        std::string_view second_geojson,
        std::string_view source_crs) const;

    [[nodiscard]] GeometryMeasurementResult AreaSquareMeters(
        std::string_view geojson,
        std::string_view source_crs) const;

    // The returned GeoJSON is transformed back to source_crs so it can be
    // combined with the caller's original layers without another conversion.
    [[nodiscard]] GeometryOperationResult BufferMeters(
        std::string_view geojson,
        std::string_view source_crs,
        double distance_meters,
        int quadrant_segments = 8) const;
};

}  // namespace gis
