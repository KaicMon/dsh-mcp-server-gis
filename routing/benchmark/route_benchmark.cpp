#include "routing/router.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

struct Samples {
    std::vector<double> latency_us;
    std::uint64_t expanded_nodes = 0;
    std::uint64_t successful = 0;
};

double Percentile(std::vector<double> values, double percentile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

routing::RouteResult Measure(const routing::Router& router,
                             routing::NodeId source,
                             routing::NodeId target,
                             routing::SearchAlgorithm algorithm,
                             Samples& samples) {
    const auto start = std::chrono::steady_clock::now();
    auto result = router.Route(
        source, target, {algorithm, routing::CostMetric::Duration, 2'000'000});
    const auto end = std::chrono::steady_clock::now();
    samples.latency_us.push_back(
        std::chrono::duration<double, std::micro>(end - start).count());
    samples.expanded_nodes += result.statistics.expanded_nodes;
    if (result.status == routing::RouteStatus::Ok) ++samples.successful;
    return result;
}

void Print(const std::string& name, const Samples& samples) {
    std::cout << name
              << " successful=" << samples.successful
              << " p50_us=" << Percentile(samples.latency_us, 0.50)
              << " p95_us=" << Percentile(samples.latency_us, 0.95)
              << " p99_us=" << Percentile(samples.latency_us, 0.99)
              << " avg_expanded="
              << (samples.latency_us.empty() ? 0 : samples.expanded_nodes / samples.latency_us.size())
              << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: route_benchmark <network.route> [query-count]\n";
        return 2;
    }
    try {
        const auto graph = routing::NetworkFile::Load(argv[1]);
        const routing::Router router{graph};
        const std::uint64_t query_count = argc == 3 ? std::stoull(argv[2]) : 100;
        std::mt19937 random{20260815};
        std::uniform_int_distribution<routing::NodeId> nodes(
            0, static_cast<routing::NodeId>(graph.NodeCount() - 1));
        Samples dijkstra;
        Samples astar;
        Samples bidirectional;
        for (std::uint64_t query = 0; query < query_count; ++query) {
            const auto source = nodes(random);
            const auto target = nodes(random);
            const auto baseline = Measure(
                router, source, target, routing::SearchAlgorithm::Dijkstra, dijkstra);
            const auto informed = Measure(
                router, source, target, routing::SearchAlgorithm::AStar, astar);
            const auto bidirectional_result = Measure(
                router, source, target,
                routing::SearchAlgorithm::BidirectionalDijkstra, bidirectional);
            const auto equal = [&](const routing::RouteResult& candidate) {
                if (candidate.status != baseline.status) return false;
                return candidate.status != routing::RouteStatus::Ok ||
                       std::abs(candidate.duration_s - baseline.duration_s) < 0.01;
            };
            if (!equal(informed) || !equal(bidirectional_result)) {
                std::cerr << "Differential check failed for query " << query
                          << " source=" << source << " target=" << target << '\n';
                return 1;
            }
        }
        Print("dijkstra", dijkstra);
        Print("astar", astar);
        Print("bidirectional_dijkstra", bidirectional);
    } catch (const std::exception& error) {
        std::cerr << "route_benchmark: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
