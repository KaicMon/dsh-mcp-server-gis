#include "gis/providers/amap_provider.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace gis::providers {
namespace {

template <typename T>
ProviderResult<T> Failure(ProviderErrorCode code, std::string message,
                          bool retryable = false) {
    return {.value = std::nullopt,
            .error = {code, std::move(message), retryable},
            .provider = "amap"};
}

std::string StringValue(const json& object, const char* name) {
    const auto found = object.find(name);
    if (found == object.end() || found->is_null()) return {};
    if (found->is_string()) return found->get<std::string>();
    if (found->is_number()) return found->dump();
    return {};
}

std::string StringOrFirstValue(const json& object, const char* name) {
    const auto found = object.find(name);
    if (found != object.end() && found->is_array() && !found->empty() &&
        found->front().is_string()) {
        return found->front().get<std::string>();
    }
    return StringValue(object, name);
}

double NumberValue(const json& object, const char* name, double fallback = 0.0) {
    const std::string text = StringValue(object, name);
    if (text.empty()) return fallback;
    try {
        return std::stod(text);
    } catch (...) {
        return fallback;
    }
}

std::optional<Coordinate> ParseAmapCoordinate(const std::string& value) {
    const auto comma = value.find(',');
    if (comma == std::string::npos) return std::nullopt;
    try {
        const double longitude = std::stod(value.substr(0, comma));
        const double latitude = std::stod(value.substr(comma + 1));
        if (!std::isfinite(longitude) || !std::isfinite(latitude)) return std::nullopt;
        return Coordinate{longitude, latitude, CoordinateSystem::Gcj02};
    } catch (...) {
        return std::nullopt;
    }
}

std::string FormatCoordinate(const Coordinate& coordinate) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6)
           << coordinate.longitude << ',' << coordinate.latitude;
    return output.str();
}

ProviderError AmapBusinessError(const json& response) {
    const std::string info = StringValue(response, "info");
    const std::string code = StringValue(response, "infocode");
    if (code == "10001" || code == "10002" || code == "10007") {
        return {ProviderErrorCode::Unauthorized, info, false};
    }
    if (code == "10003" || code == "10004" || code == "10005" ||
        code == "10010" || code == "10019" || code == "10020" ||
        code == "10021") {
        return {ProviderErrorCode::RateLimited, info, true};
    }
    if (code.starts_with("2")) {
        return {ProviderErrorCode::InvalidArgument, info, false};
    }
    return {ProviderErrorCode::Unavailable,
            info.empty() ? "Amap returned an unsuccessful response" : info,
            true};
}

template <typename T>
std::optional<ProviderResult<T>> TransportFailure(const HttpResponse& response) {
    if (!response.TransportOk()) {
        auto failure = Failure<T>(ProviderErrorCode::Timeout,
                                  "Amap request failed: " + response.error, true);
        failure.attempts = response.attempts;
        return failure;
    }
    if (response.status_code == 401 || response.status_code == 403) {
        return Failure<T>(ProviderErrorCode::Unauthorized,
                          "Amap rejected the API credential");
    }
    if (response.status_code == 429) {
        return Failure<T>(ProviderErrorCode::RateLimited,
                          "Amap HTTP rate limit exceeded", true);
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        return Failure<T>(ProviderErrorCode::Unavailable,
                          "Amap returned HTTP " + std::to_string(response.status_code),
                          response.status_code >= 500);
    }
    return std::nullopt;
}

std::optional<json> ParseResponse(const HttpResponse& response) {
    try {
        return json::parse(response.body);
    } catch (...) {
        return std::nullopt;
    }
}

bool ResponseSucceeded(const json& response) {
    const auto status = response.find("status");
    return status != response.end() &&
           ((status->is_string() && *status == "1") ||
            (status->is_number_integer() && status->get<int>() == 1));
}

std::vector<Coordinate> ParsePolyline(const std::string& polyline,
                                      const CoordinateAdapter& adapter) {
    std::vector<Coordinate> output;
    std::size_t start = 0;
    while (start < polyline.size()) {
        const auto end = polyline.find(';', start);
        const std::string point = polyline.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (auto parsed = ParseAmapCoordinate(point)) {
            if (auto converted = adapter.FromProvider(*parsed)) {
                output.push_back(*converted);
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return output;
}

std::string LineStringGeoJson(const std::vector<Coordinate>& coordinates) {
    json points = json::array();
    for (const auto& coordinate : coordinates) {
        points.push_back(json::array({coordinate.longitude, coordinate.latitude}));
    }
    return json{{"type", "LineString"}, {"coordinates", std::move(points)}}.dump();
}

}  // namespace

AmapProvider::AmapProvider(AmapProviderConfig config,
                           std::shared_ptr<HttpClient> http,
                           std::shared_ptr<CoordinateAdapter> coordinates)
    : config_(std::move(config)),
      http_(std::move(http)),
      coordinates_(std::move(coordinates)) {
    if (config_.api_key.empty() || config_.base_url.empty() ||
        config_.timeout <= std::chrono::milliseconds::zero() ||
        !http_ || !coordinates_) {
        throw std::invalid_argument("Invalid AmapProvider configuration");
    }
    while (config_.base_url.ends_with('/')) config_.base_url.pop_back();
}

std::string_view AmapProvider::Name() const noexcept { return "amap"; }

ProviderResult<GeocodeResponse> AmapProvider::Geocode(
    const GeocodeRequest& request) {
    if (request.query.empty() || request.limit == 0 || request.limit > 50) {
        return Failure<GeocodeResponse>(ProviderErrorCode::InvalidArgument,
                                         "Geocode query and limit are invalid");
    }
    QueryParameters parameters{{"key", config_.api_key},
                               {"address", request.query},
                               {"output", "JSON"}};
    if (!request.city.empty()) parameters.emplace_back("city", request.city);
    const HttpResponse http = http_->Get(config_.base_url + "/v3/geocode/geo",
                                         parameters, config_.timeout);
    if (auto failure = TransportFailure<GeocodeResponse>(http)) return *failure;
    const auto response = ParseResponse(http);
    if (!response) {
        return Failure<GeocodeResponse>(ProviderErrorCode::Unavailable,
                                         "Amap returned invalid JSON", true);
    }
    if (!ResponseSucceeded(*response)) {
        const ProviderError error = AmapBusinessError(*response);
        return {.value = std::nullopt, .error = error, .provider = "amap"};
    }

    GeocodeResponse result;
    const auto geocodes = response->find("geocodes");
    if (geocodes != response->end() && geocodes->is_array()) {
        for (const auto& item : *geocodes) {
            if (result.candidates.size() >= request.limit) break;
            auto provider_coordinate = ParseAmapCoordinate(StringValue(item, "location"));
            if (!provider_coordinate) continue;
            auto wgs84 = coordinates_->FromProvider(*provider_coordinate);
            if (!wgs84) continue;
            result.candidates.push_back({
                .name = StringValue(item, "formatted_address"),
                .formatted_address = StringValue(item, "formatted_address"),
                .coordinate = *wgs84,
                .provider_id = StringValue(item, "adcode"),
            });
        }
    }
    if (result.candidates.empty()) {
        return Failure<GeocodeResponse>(ProviderErrorCode::NotFound,
                                         "Amap found no matching address");
    }
    return {.value = std::move(result), .error = {}, .provider = "amap",
            .attempts = http.attempts, .fallback_used = false,
            .cache_hit = http.cache_hit};
}

ProviderResult<ReverseGeocodeResponse> AmapProvider::ReverseGeocode(
    const ReverseGeocodeRequest& request) {
    if (request.radius_meters <= 0.0 || request.radius_meters > 3000.0) {
        return Failure<ReverseGeocodeResponse>(ProviderErrorCode::InvalidArgument,
                                                "Reverse-geocode radius is invalid");
    }
    const auto provider_coordinate = coordinates_->ToProvider(request.coordinate);
    if (!provider_coordinate) {
        return Failure<ReverseGeocodeResponse>(ProviderErrorCode::InvalidArgument,
                                                "Unable to convert query coordinate");
    }
    const QueryParameters parameters{
        {"key", config_.api_key}, {"location", FormatCoordinate(*provider_coordinate)},
        {"radius", std::to_string(static_cast<int>(request.radius_meters))},
        {"extensions", "base"}, {"output", "JSON"},
    };
    const HttpResponse http = http_->Get(config_.base_url + "/v3/geocode/regeo",
                                         parameters, config_.timeout);
    if (auto failure = TransportFailure<ReverseGeocodeResponse>(http)) return *failure;
    const auto response = ParseResponse(http);
    if (!response) {
        return Failure<ReverseGeocodeResponse>(ProviderErrorCode::Unavailable,
                                                "Amap returned invalid JSON", true);
    }
    if (!ResponseSucceeded(*response)) {
        return {.value = std::nullopt, .error = AmapBusinessError(*response),
                .provider = "amap"};
    }
    const auto regeocode = response->find("regeocode");
    if (regeocode == response->end() || !regeocode->is_object()) {
        return Failure<ReverseGeocodeResponse>(ProviderErrorCode::NotFound,
                                                "Amap found no address at this coordinate");
    }
    const json component = regeocode->value("addressComponent", json::object());
    ReverseGeocodeResponse result{
        .formatted_address = StringValue(*regeocode, "formatted_address"),
        .province = StringOrFirstValue(component, "province"),
        .city = StringOrFirstValue(component, "city"),
        .district = StringOrFirstValue(component, "district"),
        .adcode = StringValue(component, "adcode"),
        .coordinate = request.coordinate,
    };
    return {.value = std::move(result), .error = {}, .provider = "amap",
            .attempts = http.attempts, .fallback_used = false,
            .cache_hit = http.cache_hit};
}

ProviderResult<NearbySearchResponse> AmapProvider::SearchNearby(
    const NearbySearchRequest& request) {
    if (request.radius_meters <= 0.0 || request.radius_meters > 50'000.0 ||
        request.limit == 0 || request.limit > 25) {
        return Failure<NearbySearchResponse>(ProviderErrorCode::InvalidArgument,
                                              "Nearby search limits are invalid");
    }
    const auto center = coordinates_->ToProvider(request.center);
    if (!center) {
        return Failure<NearbySearchResponse>(ProviderErrorCode::InvalidArgument,
                                              "Unable to convert search coordinate");
    }
    QueryParameters parameters{
        {"key", config_.api_key}, {"location", FormatCoordinate(*center)},
        {"radius", std::to_string(static_cast<int>(request.radius_meters))},
        {"offset", std::to_string(request.limit)}, {"page", "1"},
        {"sortrule", "distance"}, {"extensions", "base"}, {"output", "JSON"},
    };
    if (!request.keyword.empty()) parameters.emplace_back("keywords", request.keyword);
    if (!request.category.empty()) parameters.emplace_back("types", request.category);
    const HttpResponse http = http_->Get(config_.base_url + "/v3/place/around",
                                         parameters, config_.timeout);
    if (auto failure = TransportFailure<NearbySearchResponse>(http)) return *failure;
    const auto response = ParseResponse(http);
    if (!response) {
        return Failure<NearbySearchResponse>(ProviderErrorCode::Unavailable,
                                              "Amap returned invalid JSON", true);
    }
    if (!ResponseSucceeded(*response)) {
        return {.value = std::nullopt,
                .error = AmapBusinessError(*response), .provider = "amap"};
    }

    NearbySearchResponse result;
    const auto pois = response->find("pois");
    if (pois != response->end() && pois->is_array()) {
        for (const auto& item : *pois) {
            auto provider_coordinate = ParseAmapCoordinate(StringValue(item, "location"));
            if (!provider_coordinate) continue;
            auto wgs84 = coordinates_->FromProvider(*provider_coordinate);
            if (!wgs84) continue;
            result.pois.push_back({
                .id = StringValue(item, "id"),
                .name = StringValue(item, "name"),
                .category = StringValue(item, "type"),
                .address = StringValue(item, "address"),
                .coordinate = *wgs84,
                .distance_meters = NumberValue(item, "distance"),
            });
        }
    }
    return {.value = std::move(result), .error = {}, .provider = "amap",
            .attempts = http.attempts, .fallback_used = false,
            .cache_hit = http.cache_hit};
}

ProviderResult<PoiSearchResponse> AmapProvider::SearchPoi(
    const PoiSearchRequest& request) {
    if (request.keyword.empty() || request.limit == 0 || request.limit > 25) {
        return Failure<PoiSearchResponse>(ProviderErrorCode::InvalidArgument,
                                          "POI keyword and limit are invalid");
    }
    QueryParameters parameters{
        {"key", config_.api_key}, {"keywords", request.keyword},
        {"offset", std::to_string(request.limit)}, {"page", "1"},
        {"extensions", "base"}, {"output", "JSON"},
    };
    if (!request.city.empty()) parameters.emplace_back("city", request.city);
    if (!request.category.empty()) parameters.emplace_back("types", request.category);
    const HttpResponse http = http_->Get(config_.base_url + "/v3/place/text",
                                         parameters, config_.timeout);
    if (auto failure = TransportFailure<PoiSearchResponse>(http)) return *failure;
    const auto response = ParseResponse(http);
    if (!response) {
        return Failure<PoiSearchResponse>(ProviderErrorCode::Unavailable,
                                          "Amap returned invalid JSON", true);
    }
    if (!ResponseSucceeded(*response)) {
        return {.value = std::nullopt, .error = AmapBusinessError(*response),
                .provider = "amap"};
    }
    PoiSearchResponse result;
    const auto pois = response->find("pois");
    if (pois != response->end() && pois->is_array()) {
        for (const auto& item : *pois) {
            auto provider_point = ParseAmapCoordinate(StringValue(item, "location"));
            if (!provider_point) continue;
            auto wgs84 = coordinates_->FromProvider(*provider_point);
            if (!wgs84) continue;
            result.pois.push_back({
                .id = StringValue(item, "id"), .name = StringValue(item, "name"),
                .category = StringValue(item, "type"),
                .address = StringOrFirstValue(item, "address"),
                .coordinate = *wgs84, .distance_meters = 0.0,
            });
        }
    }
    return {.value = std::move(result), .error = {}, .provider = "amap",
            .attempts = http.attempts, .fallback_used = false,
            .cache_hit = http.cache_hit};
}

ProviderResult<AdministrativeSearchResponse> AmapProvider::SearchAdministrative(
    const AdministrativeSearchRequest& request) {
    if (request.keyword.empty() || request.subdistrict < 0 ||
        request.subdistrict > 3 || request.limit == 0 || request.limit > 100) {
        return Failure<AdministrativeSearchResponse>(
            ProviderErrorCode::InvalidArgument,
            "Administrative keyword, depth, or limit is invalid");
    }
    const QueryParameters parameters{
        {"key", config_.api_key}, {"keywords", request.keyword},
        {"subdistrict", std::to_string(request.subdistrict)},
        {"extensions", "base"}, {"output", "JSON"},
    };
    const HttpResponse http = http_->Get(config_.base_url + "/v3/config/district",
                                         parameters, config_.timeout);
    if (auto failure = TransportFailure<AdministrativeSearchResponse>(http)) return *failure;
    const auto response = ParseResponse(http);
    if (!response) {
        return Failure<AdministrativeSearchResponse>(ProviderErrorCode::Unavailable,
                                                       "Amap returned invalid JSON", true);
    }
    if (!ResponseSucceeded(*response)) {
        return {.value = std::nullopt, .error = AmapBusinessError(*response),
                .provider = "amap"};
    }
    AdministrativeSearchResponse result;
    const auto districts = response->find("districts");
    if (districts != response->end() && districts->is_array()) {
        for (const auto& item : *districts) {
            if (result.areas.size() >= request.limit) break;
            auto provider_center = ParseAmapCoordinate(StringValue(item, "center"));
            if (!provider_center) continue;
            auto wgs84 = coordinates_->FromProvider(*provider_center);
            if (!wgs84) continue;
            result.areas.push_back({.name = StringValue(item, "name"),
                                    .adcode = StringValue(item, "adcode"),
                                    .level = StringValue(item, "level"),
                                    .center = *wgs84});
        }
    }
    if (result.areas.empty()) {
        return Failure<AdministrativeSearchResponse>(ProviderErrorCode::NotFound,
                                                       "Amap found no administrative area");
    }
    return {.value = std::move(result), .error = {}, .provider = "amap",
            .attempts = http.attempts, .fallback_used = false,
            .cache_hit = http.cache_hit};
}

ProviderResult<RouteResponse> AmapProvider::PlanRoute(const RouteRequest& request) {
    if (request.profile == RouteProfile::Cycling) {
        return Failure<RouteResponse>(ProviderErrorCode::Unsupported,
                                       "Amap cycling v4 normalization is not enabled yet");
    }
    const auto source = coordinates_->ToProvider(request.source);
    const auto target = coordinates_->ToProvider(request.target);
    if (!source || !target) {
        return Failure<RouteResponse>(ProviderErrorCode::InvalidArgument,
                                       "Unable to convert route coordinates");
    }
    const std::string path = request.profile == RouteProfile::Walking
        ? "/v3/direction/walking" : "/v3/direction/driving";
    const QueryParameters parameters{
        {"key", config_.api_key}, {"origin", FormatCoordinate(*source)},
        {"destination", FormatCoordinate(*target)}, {"output", "JSON"},
    };
    const HttpResponse http = http_->Get(config_.base_url + path,
                                         parameters, config_.timeout);
    if (auto failure = TransportFailure<RouteResponse>(http)) return *failure;
    const auto response = ParseResponse(http);
    if (!response) {
        return Failure<RouteResponse>(ProviderErrorCode::Unavailable,
                                       "Amap returned invalid JSON", true);
    }
    if (!ResponseSucceeded(*response)) {
        return {.value = std::nullopt,
                .error = AmapBusinessError(*response), .provider = "amap"};
    }

    const auto route = response->find("route");
    if (route == response->end() || !route->is_object()) {
        return Failure<RouteResponse>(ProviderErrorCode::NotFound,
                                       "Amap returned no route");
    }
    const auto paths = route->find("paths");
    if (paths == route->end() || !paths->is_array() || paths->empty()) {
        return Failure<RouteResponse>(ProviderErrorCode::NotFound,
                                       "Amap returned no route path");
    }
    const json& selected = paths->front();
    std::vector<Coordinate> geometry;
    const auto steps = selected.find("steps");
    if (steps != selected.end() && steps->is_array()) {
        for (const auto& step : *steps) {
            auto points = ParsePolyline(StringValue(step, "polyline"), *coordinates_);
            if (!geometry.empty() && !points.empty() &&
                std::abs(geometry.back().longitude - points.front().longitude) < 1e-10 &&
                std::abs(geometry.back().latitude - points.front().latitude) < 1e-10) {
                points.erase(points.begin());
            }
            geometry.insert(geometry.end(), points.begin(), points.end());
        }
    }
    if (geometry.size() < 2) {
        return Failure<RouteResponse>(ProviderErrorCode::Unavailable,
                                       "Amap route contains no usable geometry", true);
    }
    RouteResponse result{
        .distance_meters = NumberValue(selected, "distance"),
        .duration_seconds = NumberValue(selected, "duration"),
        .geometry_geojson = LineStringGeoJson(geometry),
    };
    return {.value = std::move(result), .error = {}, .provider = "amap",
            .attempts = http.attempts, .fallback_used = false,
            .cache_hit = http.cache_hit};
}

ProviderHealth AmapProvider::HealthCheck() {
    return {.healthy = !config_.api_key.empty(),
            .message = "Amap provider configuration is ready"};
}

}  // namespace gis::providers
