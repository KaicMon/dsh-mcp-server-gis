#include "gis/providers/fallback_map_provider.h"

#include <stdexcept>
#include <utility>

namespace gis::providers {
namespace {

template <typename Response, typename Call>
ProviderResult<Response> ExecuteWithFallback(MapProvider& primary,
                                             MapProvider& secondary,
                                             Call&& call) {
    ProviderResult<Response> first = call(primary);
    if (first.Ok() || !first.error.retryable) return first;

    ProviderResult<Response> second = call(secondary);
    second.attempts += first.attempts;
    second.fallback_used = true;
    return second;
}

}  // namespace

FallbackMapProvider::FallbackMapProvider(std::shared_ptr<MapProvider> primary,
                                         std::shared_ptr<MapProvider> secondary)
    : primary_(std::move(primary)), secondary_(std::move(secondary)) {
    if (!primary_ || !secondary_) {
        throw std::invalid_argument("FallbackMapProvider requires two providers");
    }
}

std::string_view FallbackMapProvider::Name() const noexcept {
    return "fallback";
}

ProviderResult<GeocodeResponse> FallbackMapProvider::Geocode(
    const GeocodeRequest& request) {
    return ExecuteWithFallback<GeocodeResponse>(
        *primary_, *secondary_, [&](MapProvider& provider) {
            return provider.Geocode(request);
        });
}

ProviderResult<ReverseGeocodeResponse> FallbackMapProvider::ReverseGeocode(
    const ReverseGeocodeRequest& request) {
    return ExecuteWithFallback<ReverseGeocodeResponse>(
        *primary_, *secondary_, [&](MapProvider& provider) {
            return provider.ReverseGeocode(request);
        });
}

ProviderResult<NearbySearchResponse> FallbackMapProvider::SearchNearby(
    const NearbySearchRequest& request) {
    return ExecuteWithFallback<NearbySearchResponse>(
        *primary_, *secondary_, [&](MapProvider& provider) {
            return provider.SearchNearby(request);
        });
}

ProviderResult<PoiSearchResponse> FallbackMapProvider::SearchPoi(
    const PoiSearchRequest& request) {
    return ExecuteWithFallback<PoiSearchResponse>(
        *primary_, *secondary_, [&](MapProvider& provider) {
            return provider.SearchPoi(request);
        });
}

ProviderResult<AdministrativeSearchResponse>
FallbackMapProvider::SearchAdministrative(
    const AdministrativeSearchRequest& request) {
    return ExecuteWithFallback<AdministrativeSearchResponse>(
        *primary_, *secondary_, [&](MapProvider& provider) {
            return provider.SearchAdministrative(request);
        });
}

ProviderResult<RouteResponse> FallbackMapProvider::PlanRoute(
    const RouteRequest& request) {
    return ExecuteWithFallback<RouteResponse>(
        *primary_, *secondary_, [&](MapProvider& provider) {
            return provider.PlanRoute(request);
        });
}

ProviderHealth FallbackMapProvider::HealthCheck() {
    const ProviderHealth primary = primary_->HealthCheck();
    if (primary.healthy) return primary;
    const ProviderHealth secondary = secondary_->HealthCheck();
    if (secondary.healthy) {
        return {.healthy = true,
                .message = "primary unhealthy; secondary is available"};
    }
    return {.healthy = false,
            .message = "both primary and secondary providers are unhealthy"};
}

}  // namespace gis::providers
