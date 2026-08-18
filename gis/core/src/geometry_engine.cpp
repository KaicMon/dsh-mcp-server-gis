#include "gis/geometry_engine.h"

#include "gis/gdal_runtime.h"

#include <algorithm>
#include <memory>
#include <cmath>
#include <string>

#include <cpl_conv.h>
#include <ogr_api.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

namespace gis {
namespace {

struct GeometryDeleter {
    void operator()(OGRGeometry* geometry) const noexcept {
        OGRGeometryFactory::destroyGeometry(geometry);
    }
};

using GeometryPtr = std::unique_ptr<OGRGeometry, GeometryDeleter>;

struct TransformDeleter {
    void operator()(OGRCoordinateTransformation* transform) const noexcept {
        OCTDestroyCoordinateTransformation(transform);
    }
};

using TransformPtr =
    std::unique_ptr<OGRCoordinateTransformation, TransformDeleter>;

GeometryPtr ParseGeometry(std::string_view geojson) {
    if (geojson.empty()) return {};
    const std::string input(geojson);
    return GeometryPtr(OGRGeometryFactory::createFromGeoJson(input.c_str()));
}

bool ParseSpatialReference(std::string_view definition,
                           OGRSpatialReference& reference) {
    if (definition.empty()) return false;
    const std::string owned(definition);
    if (reference.SetFromUserInput(owned.c_str()) != OGRERR_NONE) return false;
    reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return true;
}

bool TransformGeometry(OGRGeometry& geometry, OGRSpatialReference& source,
                       OGRSpatialReference& target) {
    geometry.assignSpatialReference(&source);
    TransformPtr transform(OGRCreateCoordinateTransformation(&source, &target));
    return transform && geometry.transform(transform.get()) == OGRERR_NONE;
}

// Select a UTM zone from the geometry centre. UTM gives metre units and much
// lower local distortion than Web Mercator for distance, area and buffering.
// Polar data falls back to Web Mercator for now and remains an explicit future
// extension point for UPS projections.
bool BuildMetricReference(const OGRGeometry& geometry,
                          OGRSpatialReference& source,
                          OGRSpatialReference& metric) {
    OGREnvelope envelope;
    geometry.getEnvelope(&envelope);
    double x = (envelope.MinX + envelope.MaxX) * 0.5;
    double y = (envelope.MinY + envelope.MaxY) * 0.5;

    OGRSpatialReference wgs84;
    if (!ParseSpatialReference("EPSG:4326", wgs84)) return false;
    TransformPtr to_wgs84(OGRCreateCoordinateTransformation(&source, &wgs84));
    if (!to_wgs84 || !to_wgs84->Transform(1, &x, &y)) return false;

    int epsg = 3857;
    if (std::isfinite(x) && std::isfinite(y) && y >= -80.0 && y <= 84.0) {
        const int zone = std::clamp(
            static_cast<int>(std::floor((x + 180.0) / 6.0)) + 1, 1, 60);
        epsg = (y >= 0.0 ? 32600 : 32700) + zone;
    }

    const std::string definition = "EPSG:" + std::to_string(epsg);
    return ParseSpatialReference(definition, metric);
}

struct MetricGeometry {
    GeometryPtr geometry;
    OGRSpatialReference source;
    OGRSpatialReference metric;
    std::string error;
};

MetricGeometry ToMetricGeometry(std::string_view geojson,
                                std::string_view source_crs) {
    MetricGeometry result;
    result.geometry = ParseGeometry(geojson);
    if (!result.geometry || result.geometry->IsEmpty() ||
        !result.geometry->IsValid()) {
        result.error = "Invalid or empty GeoJSON geometry";
        return result;
    }
    if (!ParseSpatialReference(source_crs, result.source)) {
        result.error = "Invalid or unsupported source CRS";
        result.geometry.reset();
        return result;
    }
    if (!BuildMetricReference(*result.geometry, result.source, result.metric)) {
        result.error = "Unable to select a metric analysis CRS";
        result.geometry.reset();
        return result;
    }
    if (!TransformGeometry(*result.geometry, result.source, result.metric)) {
        result.error = "Unable to transform geometry into metric CRS";
        result.geometry.reset();
    }
    return result;
}

std::string ExportGeoJson(const OGRGeometry& geometry) {
    char* raw = geometry.exportToJson();
    if (raw == nullptr) return {};
    std::string output(raw);
    CPLFree(raw);
    return output;
}

}  // namespace

GeometryValidationResult GeometryEngine::ValidateGeoJson(
    std::string_view geojson) const {
    GdalRuntime::EnsureInitialized();

    if (geojson.empty()) {
        return {
            .valid = false,
            .geometry_type = {},
            .error = "GeoJSON must not be empty",
        };
    }

    // createFromGeoJson expects a null-terminated string, while string_view
    // does not provide that guarantee. The owned copy also keeps input lifetime
    // independent from GDAL's parsing implementation.
    GeometryPtr geometry = ParseGeometry(geojson);
    if (!geometry) {
        return {
            .valid = false,
            .geometry_type = {},
            .error = "Invalid or unsupported GeoJSON geometry",
        };
    }

    if (geometry->IsEmpty()) {
        return {
            .valid = false,
            .geometry_type = geometry->getGeometryName(),
            .error = "GeoJSON geometry must not be empty",
        };
    }

    // IsValid performs the topology check when GDAL was built with GEOS. It is
    // intentionally kept behind this facade so plugins do not depend on the
    // concrete geometry backend or its ownership rules.
    if (!geometry->IsValid()) {
        return {
            .valid = false,
            .geometry_type = geometry->getGeometryName(),
            .error = "GeoJSON geometry is topologically invalid",
        };
    }

    return {
        .valid = true,
        .geometry_type = geometry->getGeometryName(),
        .error = {},
    };
}

SpatialPredicateResult GeometryEngine::Contains(
    std::string_view container_geojson,
    std::string_view candidate_geojson) const {
    GdalRuntime::EnsureInitialized();
    GeometryPtr container = ParseGeometry(container_geojson);
    GeometryPtr candidate = ParseGeometry(candidate_geojson);
    if (!container || !candidate || container->IsEmpty() || candidate->IsEmpty()) {
        return {.success = false, .value = false,
                .error = "Both GeoJSON geometries must be valid and non-empty"};
    }
    if (!container->IsValid() || !candidate->IsValid()) {
        return {.success = false, .value = false,
                .error = "Both GeoJSON geometries must be topologically valid"};
    }
    return {.success = true,
            .value = container->Contains(candidate.get()) != 0,
            .error = {}};
}

GeometryMeasurementResult GeometryEngine::DistanceMeters(
    std::string_view first_geojson,
    std::string_view second_geojson,
    std::string_view source_crs) const {
    GdalRuntime::EnsureInitialized();
    MetricGeometry first = ToMetricGeometry(first_geojson, source_crs);
    if (!first.geometry) return {.success = false, .value = 0.0, .error = first.error};

    GeometryPtr second = ParseGeometry(second_geojson);
    if (!second || second->IsEmpty() || !second->IsValid()) {
        return {.success = false, .value = 0.0,
                .error = "Second GeoJSON geometry must be valid and non-empty"};
    }
    if (!TransformGeometry(*second, first.source, first.metric)) {
        return {.success = false, .value = 0.0,
                .error = "Unable to transform second geometry into metric CRS"};
    }
    return {.success = true, .value = first.geometry->Distance(second.get()), .error = {}};
}

GeometryMeasurementResult GeometryEngine::AreaSquareMeters(
    std::string_view geojson,
    std::string_view source_crs) const {
    GdalRuntime::EnsureInitialized();
    MetricGeometry metric = ToMetricGeometry(geojson, source_crs);
    if (!metric.geometry) return {.success = false, .value = 0.0, .error = metric.error};
    if (metric.geometry->getDimension() != 2) {
        return {.success = false, .value = 0.0,
                .error = "Area requires a polygonal geometry"};
    }
    return {.success = true,
            .value = OGR_G_Area(reinterpret_cast<OGRGeometryH>(metric.geometry.get())),
            .error = {}};
}

GeometryOperationResult GeometryEngine::BufferMeters(
    std::string_view geojson,
    std::string_view source_crs,
    double distance_meters,
    int quadrant_segments) const {
    GdalRuntime::EnsureInitialized();
    if (!std::isfinite(distance_meters) || distance_meters < 0.0) {
        return {.success = false, .geojson = {},
                .error = "Buffer distance must be a finite non-negative number"};
    }
    if (quadrant_segments < 1 || quadrant_segments > 64) {
        return {.success = false, .geojson = {},
                .error = "quadrantSegments must be between 1 and 64"};
    }

    MetricGeometry metric = ToMetricGeometry(geojson, source_crs);
    if (!metric.geometry) return {.success = false, .geojson = {}, .error = metric.error};
    GeometryPtr buffered(metric.geometry->Buffer(distance_meters, quadrant_segments));
    if (!buffered || buffered->IsEmpty()) {
        return {.success = false, .geojson = {}, .error = "Buffer operation failed"};
    }
    if (!TransformGeometry(*buffered, metric.metric, metric.source)) {
        return {.success = false, .geojson = {},
                .error = "Unable to transform buffer back to source CRS"};
    }
    std::string output = ExportGeoJson(*buffered);
    if (output.empty()) {
        return {.success = false, .geojson = {}, .error = "Unable to serialize buffer GeoJSON"};
    }
    return {.success = true, .geojson = std::move(output), .error = {}};
}

}  // namespace gis
