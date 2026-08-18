#include "routing/topology_builder.h"

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/visitor.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace routing {
namespace {

struct ParsedWay {
    OsmWayId id = 0;
    std::vector<OsmNodeId> nodes;
    RoadAttributes attributes;
};

struct ParsedData {
    std::unordered_map<OsmNodeId, Coordinate> coordinates;
    std::vector<ParsedWay> ways;
};

std::map<std::string, std::string> CopyTags(const osmium::TagList& source) {
    std::map<std::string, std::string> tags;
    for (const auto& tag : source) {
        tags.emplace(tag.key(), tag.value());
    }
    return tags;
}

class DataHandler : public osmium::handler::Handler {
public:
    explicit DataHandler(ParsedData& data) : data_(data) {}

    void node(const osmium::Node& node) {
        if (node.location().valid()) {
            data_.coordinates.emplace(
                node.id(), Coordinate{node.location().lon(), node.location().lat()});
        }
    }

    void way(const osmium::Way& way) {
        // Filtering happens before topology construction so non-driving Ways
        // never consume intersection counters or graph memory.
        const auto tags = CopyTags(way.tags());
        if (!OsmParser::IsRoutableForMotorVehicle(tags) || way.nodes().size() < 2) {
            return;
        }

        ParsedWay parsed;
        parsed.id = way.id();
        parsed.attributes = OsmParser::ParseRoadAttributes(tags);
        parsed.nodes.reserve(way.nodes().size());
        for (const auto& ref : way.nodes()) {
            parsed.nodes.push_back(ref.ref());
        }
        data_.ways.push_back(std::move(parsed));
    }

private:
    ParsedData& data_;
};

double HaversineMeters(const Coordinate& lhs, const Coordinate& rhs) {
    constexpr double kEarthRadiusMeters = 6371008.8;
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    const double lat1 = lhs.latitude * kDegreesToRadians;
    const double lat2 = rhs.latitude * kDegreesToRadians;
    const double delta_lat = (rhs.latitude - lhs.latitude) * kDegreesToRadians;
    const double delta_lon = (rhs.longitude - lhs.longitude) * kDegreesToRadians;
    const double sin_lat = std::sin(delta_lat / 2.0);
    const double sin_lon = std::sin(delta_lon / 2.0);
    const double a = sin_lat * sin_lat + std::cos(lat1) * std::cos(lat2) * sin_lon * sin_lon;
    return 2.0 * kEarthRadiusMeters * std::asin(std::min(1.0, std::sqrt(a)));
}

double GeometryLength(const std::vector<Coordinate>& geometry) {
    double result = 0.0;
    for (std::size_t index = 1; index < geometry.size(); ++index) {
        result += HaversineMeters(geometry[index - 1], geometry[index]);
    }
    return result;
}

RawGraphEdge MakeEdge(const ParsedWay& way,
                      OsmNodeId source,
                      OsmNodeId target,
                      std::vector<Coordinate> geometry) {
    RawGraphEdge edge;
    edge.source = source;
    edge.target = target;
    edge.osm_way_id = way.id;
    edge.attributes = way.attributes;
    edge.geometry = std::move(geometry);
    edge.distance_m = GeometryLength(edge.geometry);
    const double speed_kph = edge.attributes.maxspeed_kph.value_or(
        edge.attributes.default_speed_kph);
    edge.duration_s = edge.distance_m / (speed_kph / 3.6);
    return edge;
}

}  // namespace

RawGraph TopologyBuilder::Build(const std::filesystem::path& input) const {
    if (!std::filesystem::is_regular_file(input)) {
        throw std::runtime_error("OSM input is not a regular file: " + input.string());
    }

    ParsedData data;
    DataHandler handler{data};
    osmium::io::Reader reader{input.string(),
                              osmium::osm_entity_bits::node | osmium::osm_entity_bits::way};
    osmium::apply(reader, handler);
    reader.close();

    // Occurrence counting identifies shared intersections. A repeated first
    // node in a closed Way also creates a stable split point for the ring.
    std::unordered_map<OsmNodeId, std::uint32_t> occurrences;
    std::unordered_set<OsmNodeId> topology_nodes;
    // A topology segment runs from the previous topology node to the next one.
    // Nodes between them remain only as geometry, preserving curves without
    // increasing the number of states visited by Dijkstra/A*.
    for (const auto& way : data.ways) {
        topology_nodes.insert(way.nodes.front());
        topology_nodes.insert(way.nodes.back());
        for (const OsmNodeId node : way.nodes) {
            ++occurrences[node];
        }
    }
    for (const auto& [node, count] : occurrences) {
        if (count > 1) topology_nodes.insert(node);
    }

    RawGraph graph;
    graph.nodes.reserve(topology_nodes.size());
    for (const OsmNodeId id : topology_nodes) {
        const auto coordinate = data.coordinates.find(id);
        if (coordinate == data.coordinates.end()) {
            throw std::runtime_error("Missing coordinate for OSM node " + std::to_string(id));
        }
        graph.nodes.push_back({id, coordinate->second});
    }
    std::sort(graph.nodes.begin(), graph.nodes.end(),
              [](const RawGraphNode& lhs, const RawGraphNode& rhs) {
                  return lhs.osm_id < rhs.osm_id;
              });

    for (const auto& way : data.ways) {
        std::size_t segment_begin = 0;
        for (std::size_t index = 1; index < way.nodes.size(); ++index) {
            if (!topology_nodes.contains(way.nodes[index])) continue;

            std::vector<Coordinate> geometry;
            geometry.reserve(index - segment_begin + 1);
            for (std::size_t shape = segment_begin; shape <= index; ++shape) {
                const auto coordinate = data.coordinates.find(way.nodes[shape]);
                if (coordinate == data.coordinates.end()) {
                    throw std::runtime_error(
                        "Missing coordinate for OSM node " + std::to_string(way.nodes[shape]));
                }
                geometry.push_back(coordinate->second);
            }

            const OsmNodeId source = way.nodes[segment_begin];
            const OsmNodeId target = way.nodes[index];
            // OSM node order defines the forward edge. For ordinary roads we
            // additionally emit reversed geometry; oneway=-1 emits only that
            // reversed direction, while roundabouts default to forward only.
            if (way.attributes.oneway != OnewayDirection::Reverse) {
                graph.edges.push_back(MakeEdge(way, source, target, geometry));
            }
            if (way.attributes.oneway != OnewayDirection::Forward) {
                std::reverse(geometry.begin(), geometry.end());
                graph.edges.push_back(MakeEdge(way, target, source, std::move(geometry)));
            }
            segment_begin = index;
        }
    }

    return graph;
}

}  // namespace routing
