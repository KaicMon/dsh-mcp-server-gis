#include "gis/providers/fallback_map_provider.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {

using namespace gis::providers;

class StubProvider final : public MapProvider {
public:
    StubProvider(std::string name, ProviderResult<GeocodeResponse> geocode)
        : name_(std::move(name)), geocode_(std::move(geocode)) {}

    std::string_view Name() const noexcept override { return name_; }
    ProviderResult<GeocodeResponse> Geocode(const GeocodeRequest&) override {
        ++geocode_calls;
        return geocode_;
    }
    ProviderResult<NearbySearchResponse> SearchNearby(
        const NearbySearchRequest&) override {
        return {.value = std::nullopt,
                .error = {ProviderErrorCode::Unsupported, "unsupported", false},
                .provider = name_};
    }
    ProviderResult<RouteResponse> PlanRoute(const RouteRequest&) override {
        return {.value = std::nullopt,
                .error = {ProviderErrorCode::Unsupported, "unsupported", false},
                .provider = name_};
    }
    ProviderHealth HealthCheck() override { return {.healthy = true, .message = "ok"}; }

    int geocode_calls{0};

private:
    std::string name_;
    ProviderResult<GeocodeResponse> geocode_;
};

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

ProviderResult<GeocodeResponse> Failure(ProviderErrorCode code,
                                        bool retryable,
                                        std::string provider) {
    return {.value = std::nullopt,
            .error = {code, "failed", retryable},
            .provider = std::move(provider)};
}

ProviderResult<GeocodeResponse> Success(std::string provider) {
    GeocodeResponse response;
    response.candidates.push_back({.name = "南京师范大学"});
    return {.value = std::move(response),
            .error = {},
            .provider = std::move(provider)};
}

}  // namespace

int main() {
    bool ok = true;

    auto primary_timeout = std::make_shared<StubProvider>(
        "primary", Failure(ProviderErrorCode::Timeout, true, "primary"));
    auto secondary_success = std::make_shared<StubProvider>(
        "secondary", Success("secondary"));
    FallbackMapProvider recover(primary_timeout, secondary_success);
    const auto recovered = recover.Geocode({.query = "南京师范大学"});
    ok &= Expect(recovered.Ok(), "retryable primary failure should use secondary");
    ok &= Expect(recovered.provider == "secondary" && recovered.fallback_used,
                 "fallback result must identify the actual provider");
    ok &= Expect(recovered.attempts == 2, "fallback must aggregate attempt count");

    auto primary_invalid = std::make_shared<StubProvider>(
        "primary", Failure(ProviderErrorCode::InvalidArgument, false, "primary"));
    auto unused_secondary = std::make_shared<StubProvider>(
        "secondary", Success("secondary"));
    FallbackMapProvider no_retry(primary_invalid, unused_secondary);
    const auto rejected = no_retry.Geocode({});
    ok &= Expect(!rejected.Ok() && !rejected.fallback_used,
                 "non-retryable failure must remain with primary");
    ok &= Expect(unused_secondary->geocode_calls == 0,
                 "secondary must not receive invalid requests");

    return ok ? 0 : 1;
}
