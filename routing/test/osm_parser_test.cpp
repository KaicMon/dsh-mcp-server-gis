#include "routing/osm_parser.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::filesystem::path Fixture(const std::string& name) {
    return std::filesystem::path{ROUTING_TEST_DATA_DIR} / name;
}

}  // namespace

int main() {
    const routing::OsmParser parser;

    // Fixture tests lock down OSM semantics before the larger topology builder
    // starts depending on them.
    const auto cross = parser.Inspect(Fixture("simple_cross.osm"));
    Require(cross.nodes == 5, "simple_cross node count");
    Require(cross.highway_ways == 2, "simple_cross highway count");
    Require(cross.routable_highway_ways == 2, "simple_cross routable count");
    Require(cross.explicit_maxspeed_ways == 2, "simple_cross maxspeed count");
    Require(cross.missing_node_references == 0, "simple_cross references");

    const auto oneway = parser.Inspect(Fixture("one_way.osm"));
    Require(oneway.forward_oneway_ways == 1, "forward oneway count");
    Require(oneway.reverse_oneway_ways == 1, "reverse oneway count");

    const auto roundabout = parser.Inspect(Fixture("roundabout.osm"));
    Require(roundabout.roundabout_ways == 1, "roundabout count");
    Require(roundabout.forward_oneway_ways == 1, "implicit roundabout oneway");

    const auto restriction = parser.Inspect(Fixture("turn_restriction.osm"));
    Require(restriction.turn_restrictions == 1, "turn restriction count");

    const std::map<std::string, std::string> mph_tags{
        {"highway", "primary"}, {"maxspeed", "30 mph"}, {"oneway", "-1"},
    };
    const auto attributes = routing::OsmParser::ParseRoadAttributes(mph_tags);
    Require(attributes.oneway == routing::OnewayDirection::Reverse, "oneway=-1 parsing");
    Require(attributes.maxspeed_kph.has_value(), "mph maxspeed parsing");
    Require(std::abs(*attributes.maxspeed_kph - 48.28032) < 0.001, "mph conversion");

    std::string reason;
    Require(!routing::OsmParser::IsRoutableForMotorVehicle(
                {{"highway", "footway"}}, &reason),
            "footway exclusion");
    Require(reason == "highway=footway", "footway exclusion reason");
    Require(!routing::OsmParser::IsRoutableForMotorVehicle(
                {{"highway", "services"}}, &reason),
            "non-road highway exclusion");
    Require(!routing::OsmParser::IsRoutableForMotorVehicle(
                {{"highway", "service"}, {"area", "yes"}}, &reason),
            "area highway exclusion");
    Require(reason == "area_highway", "area highway exclusion reason");

    std::cout << "osm_parser_test passed\n";
    return 0;
}
