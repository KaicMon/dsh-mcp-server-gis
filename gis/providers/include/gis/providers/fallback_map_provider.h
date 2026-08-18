#pragma once

#include "gis/providers/map_provider.h"

#include <memory>
#include <string_view>

namespace gis::providers {

// Calls the secondary provider only when the primary reports a retryable
// technical failure. Invalid requests, authentication failures, ambiguity and
// deterministic empty results stay with the primary and are not duplicated.
class FallbackMapProvider final : public MapProvider {
public:
    FallbackMapProvider(std::shared_ptr<MapProvider> primary,
                        std::shared_ptr<MapProvider> secondary);

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
    std::shared_ptr<MapProvider> primary_;
    std::shared_ptr<MapProvider> secondary_;
};

}  // namespace gis::providers
