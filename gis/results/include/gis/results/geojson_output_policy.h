#pragma once

#include "gis/results/result_publisher.h"

#include <cstddef>
#include <memory>
#include <string>

namespace gis::results {

struct GeoJsonOutputConfig {
    std::size_t max_inline_bytes{32U * 1024U};
    std::size_t max_inline_points{500};
    std::chrono::seconds ttl{std::chrono::minutes(15)};
    // Optionally publish a second copy of small inline results. This gives a
    // browser renderer a stable URL without removing the compact GeoJSON that
    // an Agent needs for subsequent tool calls.
    bool publish_inline_results{false};
};

struct GeoJsonOutput {
    bool inline_result{true};
    std::string inline_geojson;
    std::string result_id;
    std::string map_url;
};

struct GeoJsonOutputResult {
    std::optional<GeoJsonOutput> value;
    std::string error;

    [[nodiscard]] bool Ok() const noexcept { return value.has_value(); }
};

// Applies both context-size and browser-complexity limits. Large values must
// be published successfully; silently falling back to inline would defeat the
// token-safety guarantee this policy exists to provide.
class GeoJsonOutputPolicy {
public:
    GeoJsonOutputPolicy(GeoJsonOutputConfig config,
                        std::shared_ptr<ResultPublisher> publisher = nullptr);

    [[nodiscard]] GeoJsonOutputResult Prepare(
        std::string serialized_geojson, std::size_t point_count) const;

private:
    GeoJsonOutputConfig config_;
    std::shared_ptr<ResultPublisher> publisher_;
};

}  // namespace gis::results
