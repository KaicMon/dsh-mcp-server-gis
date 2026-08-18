#include "gis/gdal_runtime.h"
#include "gis/coordinate_transformer.h"
#include "gis/geometry_engine.h"

#include <cmath>
#include <iostream>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main() {
    // Calling initialization more than once verifies the process-wide guard.
    gis::GdalRuntime::EnsureInitialized();
    gis::GdalRuntime::EnsureInitialized();

    const gis::GeometryEngine engine;
    const auto point = engine.ValidateGeoJson(
        R"({"type":"Point","coordinates":[118.7969,32.0603]})");
    const auto invalid = engine.ValidateGeoJson(R"({"type":"Point"})");
    const auto invalid_polygon = engine.ValidateGeoJson(
        R"({"type":"Polygon","coordinates":[[[0,0],[2,2],[0,2],[2,0],[0,0]]]})");
    const auto empty = engine.ValidateGeoJson({});

    bool ok = true;
    ok &= Expect(point.valid, "valid Point should be accepted");
    ok &= Expect(point.geometry_type == "POINT", "geometry type should be POINT");
    ok &= Expect(!invalid.valid, "malformed Point should be rejected");
    ok &= Expect(!invalid_polygon.valid, "self-intersecting Polygon should be rejected");
    ok &= Expect(!empty.valid, "empty input should be rejected");

    const gis::CoordinateTransformer transformer;
    const auto projected = transformer.Transform(
        {.x = 118.7969, .y = 32.0603}, "EPSG:4326", "EPSG:3857");
    ok &= Expect(projected.success, "WGS84 to Web Mercator should succeed");
    ok &= Expect(projected.coordinate.x > 13'000'000.0 &&
                     projected.coordinate.x < 14'000'000.0,
                 "longitude must remain the x axis");

    const auto round_trip = transformer.Transform(
        projected.coordinate, "EPSG:3857", "EPSG:4326");
    ok &= Expect(round_trip.success, "Web Mercator round trip should succeed");
    ok &= Expect(std::abs(round_trip.coordinate.x - 118.7969) < 1e-6 &&
                     std::abs(round_trip.coordinate.y - 32.0603) < 1e-6,
                 "coordinate round trip should retain longitude and latitude");

    const auto bad_crs = transformer.Transform(
        {.x = 118.7969, .y = 32.0603}, "NOT_A_CRS", "EPSG:3857");
    ok &= Expect(!bad_crs.success, "invalid CRS should be rejected");

    const std::string polygon =
        R"({"type":"Polygon","coordinates":[[[118.79,32.05],[118.81,32.05],[118.81,32.07],[118.79,32.07],[118.79,32.05]]]})";
    const std::string inside =
        R"({"type":"Point","coordinates":[118.80,32.06]})";
    const std::string outside =
        R"({"type":"Point","coordinates":[118.90,32.06]})";
    const auto contains_inside = engine.Contains(polygon, inside);
    const auto contains_outside = engine.Contains(polygon, outside);
    ok &= Expect(contains_inside.success && contains_inside.value,
                 "polygon should contain its interior point");
    ok &= Expect(contains_outside.success && !contains_outside.value,
                 "polygon should not contain an exterior point");

    const auto distance = engine.DistanceMeters(inside, outside, "EPSG:4326");
    ok &= Expect(distance.success && distance.value > 9'000.0 &&
                     distance.value < 10'000.0,
                 "metric distance should be computed after local projection");

    const auto area = engine.AreaSquareMeters(polygon, "EPSG:4326");
    ok &= Expect(area.success && area.value > 3'000'000.0 &&
                     area.value < 5'000'000.0,
                 "geographic polygon area should be returned in square metres");

    const auto buffered = engine.BufferMeters(inside, "EPSG:4326", 1000.0);
    ok &= Expect(buffered.success && !buffered.geojson.empty(),
                 "metric buffer should return GeoJSON in the source CRS");
    const auto buffered_geometry = engine.ValidateGeoJson(buffered.geojson);
    ok &= Expect(buffered_geometry.valid && buffered_geometry.geometry_type == "POLYGON",
                 "buffer output should be a valid polygon");
    return ok ? 0 : 1;
}
