#pragma once

#include "routing/raw_graph.h"

#include <filesystem>

namespace routing {

class TopologyBuilder {
public:
    // Loads routable Ways, splits them at endpoints/intersections, and emits a
    // directed graph while retaining intermediate coordinates as edge geometry.
    [[nodiscard]] RawGraph Build(const std::filesystem::path& input) const;
};

}  // namespace routing
