#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gis::providers {

enum class CoordinateSystem { Wgs84, Gcj02 };

struct Coordinate {
    double longitude{0.0};
    double latitude{0.0};
    CoordinateSystem system{CoordinateSystem::Wgs84};
};

enum class ProviderErrorCode {
    None,
    InvalidArgument,
    NotFound,
    AmbiguousLocation,
    Unauthorized,
    RateLimited,
    Timeout,
    Unavailable,
    Unsupported,
    Internal,
};

struct ProviderError {
    ProviderErrorCode code{ProviderErrorCode::None};
    std::string message;
    bool retryable{false};
};

template <typename T>
struct ProviderResult {
    std::optional<T> value;
    ProviderError error;
    std::string provider;
    std::size_t attempts{1};
    bool fallback_used{false};
    bool cache_hit{false};

    [[nodiscard]] bool Ok() const noexcept { return value.has_value(); }
};

struct GeocodeRequest {
    std::string query;
    std::string city;
    std::string language{"zh-CN"};
    std::size_t limit{10};
};

struct GeocodeCandidate {
    std::string name;
    std::string formatted_address;
    Coordinate coordinate;
    std::string provider_id;
};

struct GeocodeResponse {
    std::vector<GeocodeCandidate> candidates;
};

struct ReverseGeocodeRequest {
    Coordinate coordinate;
    double radius_meters{1000.0};
};

struct ReverseGeocodeResponse {
    std::string formatted_address;
    std::string province;
    std::string city;
    std::string district;
    std::string adcode;
    Coordinate coordinate;
};

struct NearbySearchRequest {
    Coordinate center;
    double radius_meters{1000.0};
    std::string keyword;
    std::string category;
    std::size_t limit{20};
};

struct Poi {
    std::string id;
    std::string name;
    std::string category;
    std::string address;
    Coordinate coordinate;
    double distance_meters{0.0};
};

struct NearbySearchResponse {
    std::vector<Poi> pois;
};

struct PoiSearchRequest {
    std::string keyword;
    std::string city;
    std::string category;
    std::size_t limit{20};
};

struct PoiSearchResponse {
    std::vector<Poi> pois;
};

struct AdministrativeSearchRequest {
    std::string keyword;
    int subdistrict{1};
    std::size_t limit{20};
};

struct AdministrativeArea {
    std::string name;
    std::string adcode;
    std::string level;
    Coordinate center;
};

struct AdministrativeSearchResponse {
    std::vector<AdministrativeArea> areas;
};

enum class RouteProfile { Walking, Driving, Cycling };

struct RouteRequest {
    Coordinate source;
    Coordinate target;
    RouteProfile profile{RouteProfile::Walking};
};

struct RouteResponse {
    double distance_meters{0.0};
    double duration_seconds{0.0};
    std::string geometry_geojson;
};

struct ProviderHealth {
    bool healthy{false};
    std::string message;
};

// Provider implementations normalize vendor-specific coordinates, fields and
// status codes into these project-owned types. No vendor DTO crosses this seam.
class MapProvider {
public:
    virtual ~MapProvider() = default;

    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
    [[nodiscard]] virtual ProviderResult<GeocodeResponse> Geocode(
        const GeocodeRequest& request) = 0;
    [[nodiscard]] virtual ProviderResult<ReverseGeocodeResponse> ReverseGeocode(
        const ReverseGeocodeRequest&) {
        return {.value = std::nullopt,
                .error = {ProviderErrorCode::Unsupported,
                          "Provider does not support reverse geocoding", false},
                .provider = std::string(Name())};
    }
    [[nodiscard]] virtual ProviderResult<NearbySearchResponse> SearchNearby(
        const NearbySearchRequest& request) = 0;
    [[nodiscard]] virtual ProviderResult<PoiSearchResponse> SearchPoi(
        const PoiSearchRequest&) {
        return {.value = std::nullopt,
                .error = {ProviderErrorCode::Unsupported,
                          "Provider does not support POI text search", false},
                .provider = std::string(Name())};
    }
    [[nodiscard]] virtual ProviderResult<AdministrativeSearchResponse>
        SearchAdministrative(const AdministrativeSearchRequest&) {
        return {.value = std::nullopt,
                .error = {ProviderErrorCode::Unsupported,
                          "Provider does not support administrative search", false},
                .provider = std::string(Name())};
    }
    [[nodiscard]] virtual ProviderResult<RouteResponse> PlanRoute(
        const RouteRequest& request) = 0;
    [[nodiscard]] virtual ProviderHealth HealthCheck() = 0;
};

}  // namespace gis::providers
