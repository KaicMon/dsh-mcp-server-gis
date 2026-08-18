#include "routing/topology_builder.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: topology_inspect <input.osm|input.osm.pbf>\n";
        return 2;
    }

    try {
        const routing::RawGraph graph = routing::TopologyBuilder{}.Build(argv[1]);
        std::uint64_t geometry_points = 0;
        double directed_distance_m = 0.0;
        for (const auto& edge : graph.edges) {
            geometry_points += edge.geometry.size();
            directed_distance_m += edge.distance_m;
        }
        std::cout << "topology_nodes: " << graph.nodes.size() << '\n'
                  << "directed_edges: " << graph.edges.size() << '\n'
                  << "geometry_points: " << geometry_points << '\n'
                  << "directed_distance_km: " << directed_distance_m / 1000.0 << '\n';
    } catch (const std::exception& error) {
        std::cerr << "topology_inspect: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
