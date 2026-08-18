#include "gis/coordinate_transformer.h"

#include "gis/gdal_runtime.h"

#include <cmath>
#include <memory>
#include <string>

#include <ogr_spatialref.h>

namespace gis {
namespace {

struct TransformDeleter {
    void operator()(OGRCoordinateTransformation* transform) const noexcept {
        OCTDestroyCoordinateTransformation(transform);
    }
};

using TransformPtr =
    std::unique_ptr<OGRCoordinateTransformation, TransformDeleter>;

bool ParseSpatialReference(std::string_view definition,
                           OGRSpatialReference& reference) {
    if (definition.empty()) {
        return false;
    }

    const std::string owned_definition(definition);
    if (reference.SetFromUserInput(owned_definition.c_str()) != OGRERR_NONE) {
        return false;
    }

    // MCP and GeoJSON callers use conventional GIS x/y order. Without this,
    // GDAL 3 may follow the authority definition and interpret EPSG:4326 as
    // latitude/longitude, silently swapping otherwise valid coordinates.
    reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return true;
}

}  // namespace

CoordinateTransformResult CoordinateTransformer::Transform(
    const Coordinate& coordinate,
    std::string_view source_crs,
    std::string_view target_crs) const {
    GdalRuntime::EnsureInitialized();

    CoordinateTransformResult result{
        .success = false,
        .coordinate = coordinate,
        .source_crs = std::string(source_crs),
        .target_crs = std::string(target_crs),
        .error = {},
    };

    if (!std::isfinite(coordinate.x) || !std::isfinite(coordinate.y) ||
        !std::isfinite(coordinate.z)) {
        result.error = "Coordinate values must be finite";
        return result;
    }

    OGRSpatialReference source;
    if (!ParseSpatialReference(source_crs, source)) {
        result.error = "Invalid or unsupported source CRS";
        return result;
    }

    OGRSpatialReference target;
    if (!ParseSpatialReference(target_crs, target)) {
        result.error = "Invalid or unsupported target CRS";
        return result;
    }

    TransformPtr transform(
        OGRCreateCoordinateTransformation(&source, &target));
    if (!transform) {
        result.error = "Unable to create coordinate transformation";
        return result;
    }

    double x = coordinate.x;
    double y = coordinate.y;
    double z = coordinate.z;
    if (!transform->Transform(1, &x, &y, &z)) {
        result.error = "Coordinate transformation failed";
        return result;
    }

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        result.error = "Coordinate transformation produced a non-finite value";
        return result;
    }

    result.success = true;
    result.coordinate = {.x = x, .y = y, .z = z};
    return result;
}

}  // namespace gis
