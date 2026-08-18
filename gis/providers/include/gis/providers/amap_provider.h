#pragma once

#include "gis/providers/coordinate_adapter.h"
#include "gis/providers/http_client.h"
#include "gis/providers/map_provider.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace gis::providers {

struct AmapProviderConfig {
    std::string api_key;
    std::string base_url{"https://restapi.amap.com"};
    std::chrono::milliseconds timeout{std::chrono::seconds(10)};
};

class AmapProvider final : public MapProvider {
public:
    AmapProvider(AmapProviderConfig config,
                 std::shared_ptr<HttpClient> http,
                 std::shared_ptr<CoordinateAdapter> coordinates);

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
    AmapProviderConfig config_;
    std::shared_ptr<HttpClient> http_;
    std::shared_ptr<CoordinateAdapter> coordinates_;
};

}  // namespace gis::providers
