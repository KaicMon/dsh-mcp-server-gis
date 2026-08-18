#include "gis/providers/local_osm_provider.h"

#include "json.hpp"

#include <optional>
#include <string>
#include <utility>

using json = nlohmann::json;

namespace gis::providers {
namespace {

template <typename T>
ProviderResult<T> Unsupported(std::string message) {
    return {.value = std::nullopt,
            .error = {ProviderErrorCode::Unsupported, std::move(message), false},
            .provider = "local-osm"};
}

}  // namespace

LocalOsmProvider::LocalOsmProvider(const std::filesystem::path& network_file,
                                   std::string network_version)
    : store_(std::make_unique<routing::RoadNetworkStore>()) {
    store_->Publish(store_->Stage(network_file, std::move(network_version)));
    engine_ = std::make_unique<routing::RoutingEngine>(store_->Current());
}

std::string_view LocalOsmProvider::Name() const noexcept { return "local-osm"; }

ProviderResult<GeocodeResponse> LocalOsmProvider::Geocode(const GeocodeRequest&) {
    return Unsupported<GeocodeResponse>(
        "Local OSM provider does not contain a geocoding index");
}

ProviderResult<ReverseGeocodeResponse> LocalOsmProvider::ReverseGeocode(
    const ReverseGeocodeRequest&) {
    return Unsupported<ReverseGeocodeResponse>(
        "Local OSM provider does not contain a reverse-geocoding index");
}

ProviderResult<NearbySearchResponse> LocalOsmProvider::SearchNearby(
    const NearbySearchRequest&) {
    return Unsupported<NearbySearchResponse>(
        "Local OSM provider does not contain a POI index");
}

ProviderResult<PoiSearchResponse> LocalOsmProvider::SearchPoi(
    const PoiSearchRequest&) {
    return Unsupported<PoiSearchResponse>(
        "Local OSM provider does not contain a POI index");
}

ProviderResult<AdministrativeSearchResponse>
LocalOsmProvider::SearchAdministrative(const AdministrativeSearchRequest&) {
    return Unsupported<AdministrativeSearchResponse>(
        "Local OSM provider does not contain an administrative-area index");
}

ProviderResult<RouteResponse> LocalOsmProvider::PlanRoute(
    const RouteRequest& request) {
    if (request.profile != RouteProfile::Driving) {
        return Unsupported<RouteResponse>(
            "Current local OSM graph only supports the driving profile");
    }
    routing::CoordinateRouteRequest route;
    route.source = {request.source.longitude, request.source.latitude};
    route.target = {request.target.longitude, request.target.latitude};
    route.snap_radius_m = 1000.0;
    route.search.metric = routing::CostMetric::Duration;
    const auto result = engine_->Route(route);
    if (result.status != routing::RouteStatus::Ok) {
        return {.value = std::nullopt,
                .error = {ProviderErrorCode::NotFound,
                          "No local OSM route or road candidate was found", false},
                .provider = "local-osm"};
    }

    json coordinates = json::array();
    for (const auto& point : result.geometry) {
        coordinates.push_back(json::array({point.longitude, point.latitude}));
    }
    RouteResponse response{
        .distance_meters = result.distance_m,
        .duration_seconds = result.duration_s,
        .geometry_geojson = json{{"type", "LineString"},
                                {"coordinates", std::move(coordinates)}}.dump(),
    };
    return {.value = std::move(response),
            .error = {},
            .provider = "local-osm"};
}

ProviderHealth LocalOsmProvider::HealthCheck() {
    const bool ready = engine_ != nullptr && engine_->Network().Graph().NodeCount() > 0;
    return {.healthy = ready,
            .message = ready ? "Local OSM routing network is ready"
                             : "Local OSM routing network is unavailable"};
}

}  // namespace gis::providers
