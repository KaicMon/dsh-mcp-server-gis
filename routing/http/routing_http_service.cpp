#include "httplib.h"
#include "json.hpp"
#include "routing/map_matcher.h"
#include "routing/routing_engine.h"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace {

routing::Coordinate ParseCoordinate(const json& value) {
    return {value.at("longitude").get<double>(), value.at("latitude").get<double>()};
}

json CoordinateJson(routing::Coordinate coordinate) {
    return json::array({coordinate.longitude, coordinate.latitude});
}

void SendJson(httplib::Response& response, const json& value, int status = 200) {
    // CORS is intentionally permissive for the local demo. Production must
    // replace '*' with configured origins and add authentication/rate limits.
    response.status = status;
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_content(value.dump(), "application/json; charset=utf-8");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        std::cerr << "Usage: routing_http <network.route> [port] [web-root] [bind-address]\n";
        return 2;
    }
    try {
        // HTTP and MCP instantiate the same core objects. This executable is a
        // transport adapter, not an alternative routing implementation.
        routing::RoadNetworkStore store;
        store.Publish(store.Stage(argv[1], "nanjing-v1"));
        const routing::RoutingEngine engine{store.Current()};
        httplib::Server server;
        server.Get("/api/v1/status", [&](const auto&, auto& response) {
            SendJson(response, {{"networkVersion", engine.Network().Version()},
                                {"nodes", engine.Network().Graph().NodeCount()},
                                {"edges", engine.Network().Graph().EdgeCount()}});
        });
        server.Post("/api/v1/routes", [&](const httplib::Request& request,
                                           httplib::Response& response) {
            try {
                const auto body = json::parse(request.body);
                routing::CoordinateRouteRequest route;
                route.source = ParseCoordinate(body.at("source"));
                route.target = ParseCoordinate(body.at("target"));
                route.snap_radius_m = body.value("snapRadiusMeters", 1000.0);
                route.search.metric = body.value("metric", std::string{"duration"}) == "distance"
                                          ? routing::CostMetric::Distance
                                          : routing::CostMetric::Duration;
                const auto result = engine.Route(route);
                if (result.status != routing::RouteStatus::Ok) {
                    SendJson(response, {{"error", "route_not_found"}}, 404);
                    return;
                }
                json coordinates = json::array();
                for (const auto& point : result.geometry) coordinates.push_back(CoordinateJson(point));
                SendJson(response, {
                    {"type", "Feature"},
                    {"geometry", {{"type", "LineString"}, {"coordinates", coordinates}}},
                    {"properties", {{"distanceMeters", result.distance_m},
                                    {"durationSeconds", result.duration_s},
                                    {"networkVersion", result.network_version},
                                    {"sourceProjected", CoordinateJson(result.source_projected)},
                                    {"targetProjected", CoordinateJson(result.target_projected)}}}});
            } catch (const std::exception& error) {
                SendJson(response, {{"error", "invalid_request"}, {"message", error.what()}}, 400);
            }
        });
        server.Post("/api/v1/map-match", [&](const httplib::Request& request,
                                              httplib::Response& response) {
            try {
                const auto body = json::parse(request.body);
                std::vector<routing::Coordinate> observations;
                for (const auto& value : body.at("coordinates")) {
                    observations.push_back(ParseCoordinate(value));
                }
                routing::MapMatchOptions options;
                options.search_radius_m = body.value("searchRadiusMeters", 100.0);
                options.maximum_candidates = body.value("maximumCandidates", std::size_t{4});
                const auto result = routing::MapMatcher{engine}.Match(observations, options);
                if (result.status != routing::MapMatchStatus::Ok) {
                    SendJson(response, {{"error", "map_match_failed"},
                                        {"failedObservation", result.failed_observation}}, 422);
                    return;
                }
                json geometry = json::array();
                for (const auto& point : result.geometry) geometry.push_back(CoordinateJson(point));
                json matched = json::array();
                for (const auto& point : result.observations) {
                    matched.push_back({{"observed", CoordinateJson(point.observed)},
                        {"matched", CoordinateJson(point.matched)}, {"edge", point.edge},
                        {"distanceToRoadMeters", point.distance_to_road_m},
                        {"confidence", point.confidence}});
                }
                SendJson(response, {{"networkVersion", result.network_version},
                    {"logLikelihood", result.log_likelihood}, {"observations", matched},
                    {"geometry", {{"type", "LineString"}, {"coordinates", geometry}}}});
            } catch (const std::exception& error) {
                SendJson(response, {{"error", "invalid_request"}, {"message", error.what()}}, 400);
            }
        });
        // Register the demo page explicitly because the vendored httplib build
        // does not reliably serve directory index files through mount points.
        if (argc >= 4) {
            const std::string index_path = std::string{argv[3]} + "/index.html";
            std::ifstream input{index_path};
            if (!input) throw std::runtime_error("Unable to open web page: " + index_path);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            const std::string page = buffer.str();
            const auto serve_index = [page](const httplib::Request&, httplib::Response& response) {
                response.set_content(page, "text/html; charset=utf-8");
            };
            server.Get("/", serve_index);
            server.Get("/index.html", serve_index);
        }
        const int port = argc >= 3 ? std::stoi(argv[2]) : 8088;
        const std::string bind_address = argc == 5 ? argv[4] : "127.0.0.1";
        std::cout << "Routing HTTP listening on http://" << bind_address << ':' << port << '\n';
        if (!server.listen(bind_address, port)) throw std::runtime_error("HTTP listen failed");
    } catch (const std::exception& error) {
        std::cerr << "routing_http: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
