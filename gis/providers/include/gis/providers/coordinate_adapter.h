#pragma once

#include "gis/providers/map_provider.h"

#include <optional>

namespace gis::providers {

// A provider-specific coordinate seam. Amap uses GCJ-02 in mainland China,
// while the MCP-facing contract uses WGS84. Keeping conversion injectable
// makes the policy explicit and independently testable.
class CoordinateAdapter {
public:
    virtual ~CoordinateAdapter() = default;
    [[nodiscard]] virtual std::optional<Coordinate> ToProvider(
        const Coordinate& wgs84) const = 0;
    [[nodiscard]] virtual std::optional<Coordinate> FromProvider(
        const Coordinate& provider_coordinate) const = 0;
};

}  // namespace gis::providers
