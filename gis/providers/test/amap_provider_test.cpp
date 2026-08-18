#include "gis/providers/amap_provider.h"

#include <cmath>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using namespace gis::providers;

class MockHttpClient final : public HttpClient {
public:
    HttpResponse Get(const std::string& url,
                     const QueryParameters& parameters,
                     std::chrono::milliseconds) override {
        last_url = url;
        last_parameters = parameters;
        ++calls;
        if (responses.empty()) return {.error = "no mock response"};
        HttpResponse response = std::move(responses.front());
        responses.pop_front();
        return response;
    }

    std::deque<HttpResponse> responses;
    std::string last_url;
    QueryParameters last_parameters;
    int calls{0};
};

class LabelOnlyCoordinateAdapter final : public CoordinateAdapter {
public:
    std::optional<Coordinate> ToProvider(const Coordinate& coordinate) const override {
        return Coordinate{coordinate.longitude, coordinate.latitude,
                          CoordinateSystem::Gcj02};
    }
    std::optional<Coordinate> FromProvider(const Coordinate& coordinate) const override {
        return Coordinate{coordinate.longitude, coordinate.latitude,
                          CoordinateSystem::Wgs84};
    }
};

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool HasParameter(const QueryParameters& parameters,
                  const std::string& name,
                  const std::string& value) {
    for (const auto& [candidate_name, candidate_value] : parameters) {
        if (candidate_name == name && candidate_value == value) return true;
    }
    return false;
}

std::shared_ptr<AmapProvider> Provider(const std::shared_ptr<MockHttpClient>& http) {
    return std::make_shared<AmapProvider>(
        AmapProviderConfig{.api_key = "test-key", .base_url = "https://mock.amap"},
        http, std::make_shared<LabelOnlyCoordinateAdapter>());
}

}  // namespace

int main() {
    bool ok = true;

    auto geocode_http = std::make_shared<MockHttpClient>();
    geocode_http->responses.push_back({
        .status_code = 200,
        .body = R"({"status":"1","infocode":"10000","geocodes":[{"formatted_address":"江苏省南京市栖霞区南京师范大学","location":"118.910000,32.100000","adcode":"320113"}]})",
    });
    const auto geocode = Provider(geocode_http)->Geocode(
        {.query = "南京师范大学", .city = "南京", .limit = 5});
    ok &= Expect(geocode.Ok() && geocode.value->candidates.size() == 1,
                 "geocode response should be normalized");
    ok &= Expect(geocode.value->candidates.front().coordinate.system ==
                     CoordinateSystem::Wgs84,
                 "provider coordinate must be normalized to WGS84");
    ok &= Expect(geocode_http->last_url.ends_with("/v3/geocode/geo") &&
                     HasParameter(geocode_http->last_parameters, "city", "南京"),
                 "geocode request must target the documented endpoint");

    auto reverse_http = std::make_shared<MockHttpClient>();
    reverse_http->responses.push_back({
        .status_code = 200,
        .body = R"({"status":"1","regeocode":{"formatted_address":"Jiangsu Nanjing Xuanwu","addressComponent":{"province":"Jiangsu","city":"Nanjing","district":"Xuanwu","adcode":"320102"}}})",
    });
    const auto reverse = Provider(reverse_http)->ReverseGeocode(
        {.coordinate = {118.80, 32.06}, .radius_meters = 500});
    ok &= Expect(reverse.Ok() && reverse.value->adcode == "320102" &&
                     reverse.value->coordinate.system == CoordinateSystem::Wgs84,
                 "reverse-geocode response should preserve normalized WGS84 input");
    ok &= Expect(reverse_http->last_url.ends_with("/v3/geocode/regeo") &&
                     HasParameter(reverse_http->last_parameters, "radius", "500"),
                 "reverse geocode must select its endpoint and radius");

    auto nearby_http = std::make_shared<MockHttpClient>();
    nearby_http->responses.push_back({
        .status_code = 200,
        .body = R"({"status":"1","infocode":"10000","pois":[{"id":"poi-1","name":"学则路地铁站","type":"交通设施服务;地铁站","address":"2号线","location":"118.923000,32.101000","distance":"820"}]})",
    });
    const auto nearby = Provider(nearby_http)->SearchNearby(
        {.center = {118.91, 32.10}, .radius_meters = 3000,
         .keyword = "地铁站", .category = "150500", .limit = 10});
    ok &= Expect(nearby.Ok() && nearby.value->pois.size() == 1,
                 "nearby POI response should be normalized");
    ok &= Expect(nearby.value->pois.front().distance_meters == 820.0,
                 "POI distance string should become metres");
    ok &= Expect(HasParameter(nearby_http->last_parameters, "radius", "3000") &&
                     HasParameter(nearby_http->last_parameters, "keywords", "地铁站"),
                 "nearby request must preserve search constraints");

    auto poi_http = std::make_shared<MockHttpClient>();
    poi_http->responses.push_back({
        .status_code = 200,
        .body = R"({"status":"1","pois":[{"id":"poi-2","name":"Nanjing Museum","type":"museum","address":"Zhongshan East Road","location":"118.848000,32.041000"}]})",
    });
    const auto poi = Provider(poi_http)->SearchPoi(
        {.keyword = "museum", .city = "Nanjing", .limit = 5});
    ok &= Expect(poi.Ok() && poi.value->pois.size() == 1 &&
                     poi.value->pois.front().name == "Nanjing Museum",
                 "POI text search should normalize Amap records");
    ok &= Expect(poi_http->last_url.ends_with("/v3/place/text"),
                 "POI search must select the text endpoint");

    auto district_http = std::make_shared<MockHttpClient>();
    district_http->responses.push_back({
        .status_code = 200,
        .body = R"({"status":"1","districts":[{"name":"Nanjing","adcode":"320100","level":"city","center":"118.796877,32.060255"}]})",
    });
    const auto district = Provider(district_http)->SearchAdministrative(
        {.keyword = "Nanjing", .subdistrict = 1, .limit = 10});
    ok &= Expect(district.Ok() && district.value->areas.size() == 1 &&
                     district.value->areas.front().adcode == "320100",
                 "administrative search should normalize district centres");
    ok &= Expect(district_http->last_url.ends_with("/v3/config/district"),
                 "administrative search must select the district endpoint");

    auto route_http = std::make_shared<MockHttpClient>();
    route_http->responses.push_back({
        .status_code = 200,
        .body = R"({"status":"1","infocode":"10000","route":{"paths":[{"distance":"1234","duration":"900","steps":[{"polyline":"118.910000,32.100000;118.915000,32.102000"},{"polyline":"118.915000,32.102000;118.923000,32.101000"}]}]}})",
    });
    const auto route = Provider(route_http)->PlanRoute(
        {.source = {118.91, 32.10}, .target = {118.923, 32.101},
         .profile = RouteProfile::Walking});
    ok &= Expect(route.Ok() && route.value->distance_meters == 1234.0 &&
                     route.value->duration_seconds == 900.0,
                 "walking route metrics should be normalized");
    ok &= Expect(route.value->geometry_geojson.find("LineString") != std::string::npos,
                 "walking steps should be merged into GeoJSON");
    ok &= Expect(route_http->last_url.ends_with("/v3/direction/walking"),
                 "walking profile must select walking endpoint");

    auto auth_http = std::make_shared<MockHttpClient>();
    auth_http->responses.push_back({
        .status_code = 200,
        .body = R"({"status":"0","info":"INVALID_USER_KEY","infocode":"10001"})",
    });
    const auto auth = Provider(auth_http)->Geocode({.query = "南京"});
    ok &= Expect(!auth.Ok() && auth.error.code == ProviderErrorCode::Unauthorized &&
                     !auth.error.retryable,
                 "invalid key must be a non-retryable authorization error");

    auto invalid_http = std::make_shared<MockHttpClient>();
    const auto invalid = Provider(invalid_http)->SearchNearby(
        {.center = {118.9, 32.1}, .radius_meters = 60'000});
    ok &= Expect(!invalid.Ok() && invalid_http->calls == 0,
                 "invalid request must be rejected before network I/O");

    return ok ? 0 : 1;
}
