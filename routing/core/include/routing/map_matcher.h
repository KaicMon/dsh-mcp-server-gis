#pragma once

#include "routing/routing_engine.h"

#include <cstddef>
#include <limits>
#include <vector>

namespace routing {

enum class MapMatchStatus { Ok, EmptyTrace, TooManyObservations, NoCandidates, NoTransition };

struct MapMatchOptions {
    double search_radius_m = 100.0;
    std::size_t maximum_candidates = 4;
    std::size_t maximum_observations = 1000;
    double gps_sigma_m = 15.0;
    double transition_beta_m = 50.0;
    std::uint64_t maximum_transition_expansions = 200'000;
};

struct MatchedObservation {
    Coordinate observed;
    Coordinate matched;
    EdgeId edge = 0;
    double distance_to_road_m = 0.0;
    // Local emission confidence in [0, 1], not a globally normalized posterior.
    double confidence = 0.0;
};

struct MapMatchResult {
    MapMatchStatus status = MapMatchStatus::EmptyTrace;
    std::size_t failed_observation = std::numeric_limits<std::size_t>::max();
    double log_likelihood = -std::numeric_limits<double>::infinity();
    std::vector<MatchedObservation> observations;
    // Complete routed geometry between the selected candidate states.
    std::vector<Coordinate> geometry;
    std::string network_version;
};

class MapMatcher {
public:
    explicit MapMatcher(const RoutingEngine& engine) : engine_(engine) {}

    // Hidden Markov Model map matching using Gaussian emission scores and a
    // route-vs-observation distance transition score, solved with Viterbi.
    [[nodiscard]] MapMatchResult Match(
        const std::vector<Coordinate>& observations,
        const MapMatchOptions& options = {}) const;

private:
    const RoutingEngine& engine_;
};

}  // namespace routing
