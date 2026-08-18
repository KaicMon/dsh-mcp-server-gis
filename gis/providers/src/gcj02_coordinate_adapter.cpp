#include "gis/providers/gcj02_coordinate_adapter.h"

#include <cmath>
#include <optional>

namespace gis::providers {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSemiMajorAxis = 6378245.0;
constexpr double kEccentricitySquared = 0.00669342162296594323;

bool FiniteAndInRange(const Coordinate& coordinate) {
    return std::isfinite(coordinate.longitude) &&
           std::isfinite(coordinate.latitude) &&
           coordinate.longitude >= -180.0 && coordinate.longitude <= 180.0 &&
           coordinate.latitude >= -90.0 && coordinate.latitude <= 90.0;
}

bool OutsideMainlandChina(const Coordinate& coordinate) {
    return coordinate.longitude < 72.004 || coordinate.longitude > 137.8347 ||
           coordinate.latitude < 0.8293 || coordinate.latitude > 55.8271;
}

double LatitudeOffset(double x, double y) {
    double value = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y +
                   0.1 * x * y + 0.2 * std::sqrt(std::abs(x));
    value += (20.0 * std::sin(6.0 * x * kPi) +
              20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    value += (20.0 * std::sin(y * kPi) +
              40.0 * std::sin(y / 3.0 * kPi)) * 2.0 / 3.0;
    value += (160.0 * std::sin(y / 12.0 * kPi) +
              320.0 * std::sin(y * kPi / 30.0)) * 2.0 / 3.0;
    return value;
}

double LongitudeOffset(double x, double y) {
    double value = 300.0 + x + 2.0 * y + 0.1 * x * x +
                   0.1 * x * y + 0.1 * std::sqrt(std::abs(x));
    value += (20.0 * std::sin(6.0 * x * kPi) +
              20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    value += (20.0 * std::sin(x * kPi) +
              40.0 * std::sin(x / 3.0 * kPi)) * 2.0 / 3.0;
    value += (150.0 * std::sin(x / 12.0 * kPi) +
              300.0 * std::sin(x / 30.0 * kPi)) * 2.0 / 3.0;
    return value;
}

Coordinate Forward(const Coordinate& wgs84) {
    if (OutsideMainlandChina(wgs84)) {
        return {wgs84.longitude, wgs84.latitude, CoordinateSystem::Gcj02};
    }
    double latitude_delta = LatitudeOffset(
        wgs84.longitude - 105.0, wgs84.latitude - 35.0);
    double longitude_delta = LongitudeOffset(
        wgs84.longitude - 105.0, wgs84.latitude - 35.0);
    const double radians = wgs84.latitude / 180.0 * kPi;
    const double sin_latitude = std::sin(radians);
    double magic = 1.0 - kEccentricitySquared * sin_latitude * sin_latitude;
    const double sqrt_magic = std::sqrt(magic);
    latitude_delta = latitude_delta * 180.0 /
        ((kSemiMajorAxis * (1.0 - kEccentricitySquared)) /
         (magic * sqrt_magic) * kPi);
    longitude_delta = longitude_delta * 180.0 /
        (kSemiMajorAxis / sqrt_magic * std::cos(radians) * kPi);
    return {wgs84.longitude + longitude_delta,
            wgs84.latitude + latitude_delta,
            CoordinateSystem::Gcj02};
}

}  // namespace

std::optional<Coordinate> Gcj02CoordinateAdapter::ToProvider(
    const Coordinate& wgs84) const {
    if (!FiniteAndInRange(wgs84) || wgs84.system != CoordinateSystem::Wgs84) {
        return std::nullopt;
    }
    return Forward(wgs84);
}

std::optional<Coordinate> Gcj02CoordinateAdapter::FromProvider(
    const Coordinate& gcj02) const {
    if (!FiniteAndInRange(gcj02) || gcj02.system != CoordinateSystem::Gcj02) {
        return std::nullopt;
    }
    if (OutsideMainlandChina(gcj02)) {
        return Coordinate{gcj02.longitude, gcj02.latitude, CoordinateSystem::Wgs84};
    }

    // Invert the forward transform with a bounded binary search. The transform
    // is locally monotonic at LBS scale; 32 iterations converge well below the
    // precision returned by the Amap Web API.
    double min_lon = gcj02.longitude - 0.02;
    double max_lon = gcj02.longitude + 0.02;
    double min_lat = gcj02.latitude - 0.02;
    double max_lat = gcj02.latitude + 0.02;
    Coordinate estimate;
    for (int iteration = 0; iteration < 32; ++iteration) {
        estimate = {(min_lon + max_lon) * 0.5,
                    (min_lat + max_lat) * 0.5,
                    CoordinateSystem::Wgs84};
        const Coordinate projected = Forward(estimate);
        if (projected.longitude < gcj02.longitude) {
            min_lon = estimate.longitude;
        } else {
            max_lon = estimate.longitude;
        }
        if (projected.latitude < gcj02.latitude) {
            min_lat = estimate.latitude;
        } else {
            max_lat = estimate.latitude;
        }
    }
    return estimate;
}

}  // namespace gis::providers
