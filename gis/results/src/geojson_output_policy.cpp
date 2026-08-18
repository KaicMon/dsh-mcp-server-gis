#include "gis/results/geojson_output_policy.h"

#include <stdexcept>
#include <utility>

namespace gis::results {

GeoJsonOutputPolicy::GeoJsonOutputPolicy(
    GeoJsonOutputConfig config, std::shared_ptr<ResultPublisher> publisher)
    : config_(config), publisher_(std::move(publisher)) {
    if (config_.max_inline_bytes == 0 || config_.max_inline_points == 0 ||
        config_.ttl <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("Invalid GeoJsonOutputPolicy configuration");
    }
}

GeoJsonOutputResult GeoJsonOutputPolicy::Prepare(
    std::string serialized_geojson, std::size_t point_count) const {
    if (serialized_geojson.empty()) {
        return {.value = std::nullopt, .error = "GeoJSON payload must not be empty"};
    }
    if (serialized_geojson.size() <= config_.max_inline_bytes &&
        point_count <= config_.max_inline_points) {
        std::string result_id;
        std::string map_url;
        if (config_.publish_inline_results && publisher_) {
            auto published = publisher_->Publish(
                serialized_geojson, "application/geo+json", config_.ttl);
            if (!published.Ok()) {
                return {.value = std::nullopt, .error = published.error};
            }
            result_id = std::move(published.value->result_id);
            map_url = std::move(published.value->map_url);
        }
        return {.value = GeoJsonOutput{.inline_result = true,
                                      .inline_geojson = std::move(serialized_geojson),
                                      .result_id = std::move(result_id),
                                      .map_url = std::move(map_url)},
                .error = {}};
    }
    if (!publisher_) {
        return {.value = std::nullopt,
                .error = "GeoJSON exceeds inline limits and no result service is configured"};
    }
    auto published = publisher_->Publish(
        serialized_geojson, "application/geo+json", config_.ttl);
    if (!published.Ok()) return {.value = std::nullopt, .error = published.error};
    return {.value = GeoJsonOutput{.inline_result = false,
                                  .inline_geojson = {},
                                  .result_id = std::move(published.value->result_id),
                                  .map_url = std::move(published.value->map_url)},
            .error = {}};
}

}  // namespace gis::results
