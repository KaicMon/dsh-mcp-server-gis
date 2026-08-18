#include "routing/csr_graph.h"
#include "routing/topology_builder.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: osm_build <input.osm|input.osm.pbf> <output.route>\n";
        return 2;
    }
    try {
        const auto raw = routing::TopologyBuilder{}.Build(argv[1]);
        const auto graph = routing::CsrBuilder{}.Build(raw);
        routing::NetworkFile::Save(graph, argv[2]);
        std::cout << "nodes: " << graph.NodeCount() << '\n'
                  << "edges: " << graph.EdgeCount() << '\n'
                  << "geometry_points: " << graph.geometry.size() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "osm_build: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
