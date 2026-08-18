#include "routing/osm_parser.h"

#include <exception>
#include <iostream>

namespace {

void PrintMap(const std::map<std::string, std::uint64_t>& values) {
    for (const auto& [name, count] : values) {
        std::cout << "  " << name << ": " << count << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: osm_inspect <input.osm|input.osm.pbf>\n";
        return 2;
    }

    try {
        // Keep this executable as a presentation layer; parsing policy belongs
        // to routing_osm so the builder and tests reuse exactly the same rules.
        const routing::OsmStatistics stats = routing::OsmParser{}.Inspect(argv[1]);
        std::cout << "nodes: " << stats.nodes << '\n'
                  << "ways: " << stats.ways << '\n'
                  << "relations: " << stats.relations << '\n'
                  << "highway_ways: " << stats.highway_ways << '\n'
                  << "routable_highway_ways: " << stats.routable_highway_ways << '\n'
                  << "excluded_highway_ways: " << stats.excluded_highway_ways << '\n'
                  << "referenced_nodes: " << stats.referenced_nodes << '\n'
                  << "missing_node_references: " << stats.missing_node_references << '\n'
                  << "forward_oneway_ways: " << stats.forward_oneway_ways << '\n'
                  << "reverse_oneway_ways: " << stats.reverse_oneway_ways << '\n'
                  << "roundabout_ways: " << stats.roundabout_ways << '\n'
                  << "explicit_maxspeed_ways: " << stats.explicit_maxspeed_ways << '\n'
                  << "turn_restrictions: " << stats.turn_restrictions << '\n'
                  << "highway_counts:\n";
        PrintMap(stats.highway_counts);
        std::cout << "excluded_reasons:\n";
        PrintMap(stats.excluded_reasons);
    } catch (const std::exception& error) {
        std::cerr << "osm_inspect: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
