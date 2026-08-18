#pragma once

#include "gis/providers/coordinate_adapter.h"

namespace gis::providers {

// Converts between WGS84 and mainland-China GCJ-02 for LBS interoperability.
// The inverse is solved iteratively against the forward transform. It is
// appropriate for POI/routing display, not cadastral, surveying or legal use.
class Gcj02CoordinateAdapter final : public CoordinateAdapter {
public:
    [[nodiscard]] std::optional<Coordinate> ToProvider(
        const Coordinate& wgs84) const override;
    [[nodiscard]] std::optional<Coordinate> FromProvider(
        const Coordinate& gcj02) const override;
};

}  // namespace gis::providers
