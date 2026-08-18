#include "routing/map_matcher.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace routing {
namespace {

constexpr double kNegativeInfinity = -std::numeric_limits<double>::infinity();

double HaversineMeters(Coordinate lhs, Coordinate rhs) {
    constexpr double kEarthRadiusMeters = 6371008.8;
    constexpr double kRadians = 3.14159265358979323846 / 180.0;
    const double lhs_lat = lhs.latitude * kRadians;
    const double rhs_lat = rhs.latitude * kRadians;
    const double delta_lat = (rhs.latitude - lhs.latitude) * kRadians;
    const double delta_lon = (rhs.longitude - lhs.longitude) * kRadians;
    const double sin_lat = std::sin(delta_lat * 0.5);
    const double sin_lon = std::sin(delta_lon * 0.5);
    const double value = sin_lat * sin_lat +
                         std::cos(lhs_lat) * std::cos(rhs_lat) * sin_lon * sin_lon;
    return 2.0 * kEarthRadiusMeters * std::asin(std::min(1.0, std::sqrt(value)));
}

double EmissionLogProbability(double distance_m, double sigma_m) {
    // Constant normalization is omitted because it is identical for all states
    // in one observation layer and therefore cannot change the Viterbi argmax.
    const double normalized = distance_m / sigma_m;
    return -0.5 * normalized * normalized;
}

void AppendGeometry(std::vector<Coordinate>& output,
                    const std::vector<Coordinate>& input) {
    for (const auto& point : input) {
        if (output.empty() || output.back().longitude != point.longitude ||
            output.back().latitude != point.latitude) {
            output.push_back(point);
        }
    }
}

}  // namespace

MapMatchResult MapMatcher::Match(const std::vector<Coordinate>& observations,
                                 const MapMatchOptions& options) const {
    MapMatchResult result;
    result.network_version = engine_.Network().Version();
    if (observations.empty()) return result;
    if (observations.size() > options.maximum_observations) {
        result.status = MapMatchStatus::TooManyObservations;
        return result;
    }
    if (options.gps_sigma_m <= 0.0 || options.transition_beta_m <= 0.0) {
        throw std::invalid_argument("Map matching sigma and beta must be positive");
    }

    // Candidate generation is independent per observation. A missing layer
    // makes a continuous HMM path impossible and is reported with its index.
    std::vector<std::vector<RoadCandidate>> candidates;
    candidates.reserve(observations.size());
    for (std::size_t index = 0; index < observations.size(); ++index) {
        candidates.push_back(engine_.NearestRoads(
            observations[index], options.search_radius_m, options.maximum_candidates));
        if (candidates.back().empty()) {
            result.status = MapMatchStatus::NoCandidates;
            result.failed_observation = index;
            return result;
        }
    }

    // score[t][j] is the best log likelihood ending at candidate j of GPS point
    // t. predecessor[t][j] stores the candidate index selected at t-1.
    std::vector<std::vector<double>> score(observations.size());
    std::vector<std::vector<std::size_t>> predecessor(observations.size());
    score[0].resize(candidates[0].size());
    predecessor[0].assign(candidates[0].size(), 0);
    for (std::size_t state = 0; state < candidates[0].size(); ++state) {
        score[0][state] = EmissionLogProbability(
            candidates[0][state].distance_m, options.gps_sigma_m);
    }

    SearchOptions transition_search;
    transition_search.algorithm = SearchAlgorithm::AStar;
    transition_search.metric = CostMetric::Distance;
    transition_search.max_expanded_nodes = options.maximum_transition_expansions;
    for (std::size_t time = 1; time < observations.size(); ++time) {
        score[time].assign(candidates[time].size(), kNegativeInfinity);
        predecessor[time].assign(candidates[time].size(), 0);
        const double observed_distance = HaversineMeters(
            observations[time - 1], observations[time]);
        for (std::size_t current = 0; current < candidates[time].size(); ++current) {
            const double emission = EmissionLogProbability(
                candidates[time][current].distance_m, options.gps_sigma_m);
            for (std::size_t previous = 0; previous < candidates[time - 1].size(); ++previous) {
                if (!std::isfinite(score[time - 1][previous])) continue;
                const auto route = engine_.RouteCandidates(
                    candidates[time - 1][previous], candidates[time][current], transition_search);
                if (route.status != RouteStatus::Ok) continue;
                // Penalize disagreement between motion seen by GPS and motion
                // required by the directed road network.
                const double transition = -std::abs(route.distance_m - observed_distance) /
                                          options.transition_beta_m;
                const double candidate_score = score[time - 1][previous] + transition + emission;
                if (candidate_score > score[time][current]) {
                    score[time][current] = candidate_score;
                    predecessor[time][current] = previous;
                }
            }
        }
        if (std::none_of(score[time].begin(), score[time].end(),
                         [](double value) { return std::isfinite(value); })) {
            result.status = MapMatchStatus::NoTransition;
            result.failed_observation = time;
            return result;
        }
    }

    // Backtrack from the best state in the final Viterbi layer.
    std::vector<std::size_t> selected(observations.size());
    selected.back() = static_cast<std::size_t>(
        std::max_element(score.back().begin(), score.back().end()) - score.back().begin());
    result.log_likelihood = score.back()[selected.back()];
    for (std::size_t time = observations.size() - 1; time > 0; --time) {
        selected[time - 1] = predecessor[time][selected[time]];
    }

    result.observations.reserve(observations.size());
    for (std::size_t time = 0; time < observations.size(); ++time) {
        const auto& state = candidates[time][selected[time]];
        result.observations.push_back({observations[time], state.projected, state.edge,
            state.distance_m, std::exp(EmissionLogProbability(state.distance_m, options.gps_sigma_m))});
    }
    // Re-run only the selected transitions to produce display geometry. This
    // avoids retaining K^2 route geometries in every dynamic-programming layer.
    if (observations.size() == 1) {
        result.geometry.push_back(result.observations.front().matched);
    } else {
        for (std::size_t time = 1; time < observations.size(); ++time) {
            const auto route = engine_.RouteCandidates(
                candidates[time - 1][selected[time - 1]],
                candidates[time][selected[time]], transition_search);
            AppendGeometry(result.geometry, route.geometry);
        }
    }
    result.status = MapMatchStatus::Ok;
    return result;
}

}  // namespace routing
