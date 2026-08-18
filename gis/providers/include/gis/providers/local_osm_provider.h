#pragma once

#include "gis/providers/map_provider.h"

#include "routing/network_store.h"
#include "routing/routing_engine.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace gis::providers {

// Adapts the immutable local OSM routing snapshot to MapProvider. The current
// graph is motor-vehicle oriented, so walking/cycling requests are rejected
// instead of silently returning a route with the wrong access semantics.
class LocalOsmProvider final : public MapProvider {
public:
    LocalOsmProvider(const std::filesystem::path& network_file,
                     std::string network_version);

    [[nodiscard]] std::string_view Name() const noexcept override;
    [[nodiscard]] ProviderResult<GeocodeResponse> Geocode(
        const GeocodeRequest& request) override;
    [[nodiscard]] ProviderResult<ReverseGeocodeResponse> ReverseGeocode(
        const ReverseGeocodeRequest& request) override;
    [[nodiscard]] ProviderResult<NearbySearchResponse> SearchNearby(
        const NearbySearchRequest& request) override;
    [[nodiscard]] ProviderResult<PoiSearchResponse> SearchPoi(
        const PoiSearchRequest& request) override;
    [[nodiscard]] ProviderResult<AdministrativeSearchResponse>
        SearchAdministrative(const AdministrativeSearchRequest& request) override;
    [[nodiscard]] ProviderResult<RouteResponse> PlanRoute(
        const RouteRequest& request) override;
    [[nodiscard]] ProviderHealth HealthCheck() override;

private:
    std::unique_ptr<routing::RoadNetworkStore> store_;
    std::unique_ptr<routing::RoutingEngine> engine_;
};

}  // namespace gis::providers
