#pragma once

#include <string>
#include <string_view>

namespace gis {

struct Coordinate {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct CoordinateTransformResult {
    bool success{false};
    Coordinate coordinate;
    std::string source_crs;
    std::string target_crs;
    std::string error;
};

// Converts a coordinate between standard CRS definitions understood by GDAL.
// The public convention is always x/y (longitude/latitude for geographic CRS),
// independent of EPSG's authority-axis ordering.
class CoordinateTransformer final {
public:
    [[nodiscard]] CoordinateTransformResult Transform(
        const Coordinate& coordinate,
        std::string_view source_crs,
        std::string_view target_crs) const;
};

}  // namespace gis
