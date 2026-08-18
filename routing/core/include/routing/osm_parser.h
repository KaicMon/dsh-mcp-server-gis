#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace routing {

enum class OnewayDirection {
    Both,
    Forward,
    // Travel follows the reverse order of node references in the OSM Way.
    Reverse,
};

// Road-level OSM semantics retained for the topology-building phase.
struct RoadAttributes {
    std::string highway;
    std::string name;
    OnewayDirection oneway = OnewayDirection::Both;
    std::optional<double> maxspeed_kph;
    double default_speed_kph = 0.0;
    bool roundabout = false;
    bool motor_vehicle_allowed = true;
};

struct OsmStatistics {
    std::uint64_t nodes = 0;
    std::uint64_t ways = 0;
    std::uint64_t relations = 0;
    std::uint64_t highway_ways = 0;
    std::uint64_t routable_highway_ways = 0;
    std::uint64_t excluded_highway_ways = 0;
    std::uint64_t referenced_nodes = 0;
    std::uint64_t missing_node_references = 0;
    std::uint64_t forward_oneway_ways = 0;
    std::uint64_t reverse_oneway_ways = 0;
    std::uint64_t roundabout_ways = 0;
    std::uint64_t explicit_maxspeed_ways = 0;
    std::uint64_t turn_restrictions = 0;
    std::map<std::string, std::uint64_t> highway_counts;
    std::map<std::string, std::uint64_t> excluded_reasons;
};

class OsmParser {
public:
    // Scans an OSM XML/PBF file without constructing the routing graph.
    // The result is used to validate input quality and estimate graph size.
    [[nodiscard]] OsmStatistics Inspect(const std::filesystem::path& input) const;

    // Converts raw OSM tags into normalized attributes. The default speed is
    // a routing policy fallback and does not replace an explicit maxspeed.
    [[nodiscard]] static RoadAttributes ParseRoadAttributes(
        const std::map<std::string, std::string>& tags);
    // Applies the initial motor-vehicle profile. Conditional access rules are
    // intentionally deferred until a dedicated access-policy module exists.
    [[nodiscard]] static bool IsRoutableForMotorVehicle(
        const std::map<std::string, std::string>& tags,
        std::string* excluded_reason = nullptr);
};

[[nodiscard]] const char* ToString(OnewayDirection direction) noexcept;

}  // namespace routing
