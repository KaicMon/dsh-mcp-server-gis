#include "routing/osm_parser.h"

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/visitor.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace routing {
namespace {

using Tags = std::map<std::string, std::string>;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<std::string> FindTag(const Tags& tags, const std::string& key) {
    const auto it = tags.find(key);
    if (it == tags.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool IsDenied(const std::optional<std::string>& value) {
    if (!value) {
        return false;
    }
    const auto normalized = Lower(*value);
    return normalized == "no" || normalized == "private";
}

std::optional<double> ParseMaxspeed(const std::optional<std::string>& raw) {
    if (!raw) {
        return std::nullopt;
    }

    const std::string value = Lower(*raw);
    char* end = nullptr;
    const double numeric = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || numeric <= 0.0) {
        return std::nullopt;
    }

    // strtod stops before the unit, allowing both "60" and "30 mph".
    std::string_view suffix{end};
    while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.front()))) {
        suffix.remove_prefix(1);
    }
    if (suffix.starts_with("mph")) {
        return numeric * 1.609344;
    }
    return numeric;
}

double DefaultSpeed(const std::string& highway) {
    // Conservative phase-one defaults. These values become configurable when
    // driving profiles are introduced; explicit maxspeed always takes priority.
    if (highway == "motorway") return 100.0;
    if (highway == "motorway_link") return 60.0;
    if (highway == "trunk") return 80.0;
    if (highway == "trunk_link") return 50.0;
    if (highway == "primary") return 60.0;
    if (highway == "primary_link") return 40.0;
    if (highway == "secondary") return 50.0;
    if (highway == "secondary_link") return 35.0;
    if (highway == "tertiary" || highway == "tertiary_link") return 40.0;
    if (highway == "residential" || highway == "unclassified") return 30.0;
    if (highway == "living_street") return 15.0;
    if (highway == "service") return 20.0;
    return 20.0;
}

Tags CopyTags(const osmium::TagList& source) {
    Tags tags;
    for (const auto& tag : source) {
        tags.emplace(tag.key(), tag.value());
    }
    return tags;
}

class StatisticsHandler : public osmium::handler::Handler {
public:
    explicit StatisticsHandler(OsmStatistics& statistics) : statistics_(statistics) {}

    void node(const osmium::Node& node) {
        ++statistics_.nodes;
        // Geofabrik extracts are ordered nodes-before-ways. Retaining IDs lets
        // us detect incomplete Way references during this same streaming pass.
        node_ids_.insert(node.id());
    }

    void way(const osmium::Way& way) {
        ++statistics_.ways;
        const Tags tags = CopyTags(way.tags());
        const auto highway = FindTag(tags, "highway");
        if (!highway) {
            return;
        }

        ++statistics_.highway_ways;
        ++statistics_.highway_counts[*highway];
        statistics_.referenced_nodes += way.nodes().size();
        for (const auto& node_ref : way.nodes()) {
            if (!node_ids_.contains(node_ref.ref())) {
                ++statistics_.missing_node_references;
            }
        }

        // Count every highway before applying the driving profile so excluded
        // classes remain visible in input-quality reports.
        std::string reason;
        if (!OsmParser::IsRoutableForMotorVehicle(tags, &reason)) {
            ++statistics_.excluded_highway_ways;
            ++statistics_.excluded_reasons[reason];
            return;
        }

        ++statistics_.routable_highway_ways;
        const RoadAttributes attributes = OsmParser::ParseRoadAttributes(tags);
        if (attributes.oneway == OnewayDirection::Forward) {
            ++statistics_.forward_oneway_ways;
        } else if (attributes.oneway == OnewayDirection::Reverse) {
            ++statistics_.reverse_oneway_ways;
        }
        if (attributes.roundabout) ++statistics_.roundabout_ways;
        if (attributes.maxspeed_kph) ++statistics_.explicit_maxspeed_ways;
    }

    void relation(const osmium::Relation& relation) {
        ++statistics_.relations;
        const char* type = relation.tags().get_value_by_key("type");
        if (type != nullptr && std::string_view{type} == "restriction") {
            ++statistics_.turn_restrictions;
        }
    }

private:
    OsmStatistics& statistics_;
    std::unordered_set<osmium::object_id_type> node_ids_;
};

}  // namespace

OsmStatistics OsmParser::Inspect(const std::filesystem::path& input) const {
    if (!std::filesystem::is_regular_file(input)) {
        throw std::runtime_error("OSM input is not a regular file: " + input.string());
    }

    OsmStatistics statistics;
    StatisticsHandler handler{statistics};
    osmium::io::Reader reader{input.string(), osmium::osm_entity_bits::all};
    osmium::apply(reader, handler);
    reader.close();
    return statistics;
}

RoadAttributes OsmParser::ParseRoadAttributes(const Tags& tags) {
    RoadAttributes result;
    result.highway = FindTag(tags, "highway").value_or("");
    result.name = FindTag(tags, "name").value_or("");
    result.roundabout = FindTag(tags, "junction").value_or("") == "roundabout";
    result.maxspeed_kph = ParseMaxspeed(FindTag(tags, "maxspeed"));
    result.default_speed_kph = DefaultSpeed(result.highway);
    result.motor_vehicle_allowed = IsRoutableForMotorVehicle(tags);

    // Explicit oneway=no overrides the implicit one-way rule for roundabouts.
    const std::string oneway = Lower(FindTag(tags, "oneway").value_or(""));
    if (oneway == "-1" || oneway == "reverse") {
        result.oneway = OnewayDirection::Reverse;
    } else if (oneway == "yes" || oneway == "true" || oneway == "1") {
        result.oneway = OnewayDirection::Forward;
    } else if (oneway == "no" || oneway == "false" || oneway == "0") {
        result.oneway = OnewayDirection::Both;
    } else if (result.roundabout) {
        result.oneway = OnewayDirection::Forward;
    }
    return result;
}

bool OsmParser::IsRoutableForMotorVehicle(const Tags& tags, std::string* excluded_reason) {
    const auto highway = FindTag(tags, "highway");
    if (!highway) {
        if (excluded_reason) *excluded_reason = "missing_highway";
        return false;
    }

    // Use an allowlist: OSM's highway key also labels facilities and non-road
    // geometries, so accepting every unknown value would pollute the graph.
    static const std::unordered_set<std::string> routable_highways{
        "living_street", "motorway", "motorway_link", "primary", "primary_link",
        "residential", "road", "secondary", "secondary_link", "service", "tertiary",
        "tertiary_link", "track", "trunk", "trunk_link", "unclassified",
    };
    if (!routable_highways.contains(*highway)) {
        if (excluded_reason) *excluded_reason = "highway=" + *highway;
        return false;
    }

    if (Lower(FindTag(tags, "area").value_or("")) == "yes") {
        if (excluded_reason) *excluded_reason = "area_highway";
        return false;
    }

    // More specific access tags are checked first to make diagnostics useful.
    if (IsDenied(FindTag(tags, "motorcar"))) {
        if (excluded_reason) *excluded_reason = "motorcar_access";
        return false;
    }
    if (IsDenied(FindTag(tags, "motor_vehicle"))) {
        if (excluded_reason) *excluded_reason = "motor_vehicle_access";
        return false;
    }
    if (IsDenied(FindTag(tags, "vehicle"))) {
        if (excluded_reason) *excluded_reason = "vehicle_access";
        return false;
    }
    if (IsDenied(FindTag(tags, "access"))) {
        if (excluded_reason) *excluded_reason = "general_access";
        return false;
    }
    return true;
}

const char* ToString(OnewayDirection direction) noexcept {
    switch (direction) {
        case OnewayDirection::Both: return "both";
        case OnewayDirection::Forward: return "forward";
        case OnewayDirection::Reverse: return "reverse";
    }
    return "unknown";
}

}  // namespace routing
