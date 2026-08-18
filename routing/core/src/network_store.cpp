#include "routing/network_store.h"

#include <stdexcept>
#include <utility>

namespace routing {

RoadNetwork::RoadNetwork(CsrGraph graph, std::string version)
    : graph_(std::move(graph)), version_(std::move(version)) {
    if (version_.empty()) throw std::invalid_argument("Road network version must not be empty");
    graph_.Validate();
    router_ = std::make_unique<Router>(graph_);
    spatial_index_ = std::make_unique<SpatialIndex>(graph_);
}

RoadNetworkStore::Snapshot RoadNetworkStore::Stage(
    const std::filesystem::path& file, std::string version) const {
    return std::make_shared<const RoadNetwork>(NetworkFile::Load(file), std::move(version));
}

void RoadNetworkStore::Publish(Snapshot candidate) {
    if (!candidate) throw std::invalid_argument("Cannot publish an empty road network");
    std::lock_guard lock{publish_mutex_};
    previous_ = std::atomic_exchange_explicit(
        &current_, std::move(candidate), std::memory_order_acq_rel);
}

RoadNetworkStore::Snapshot RoadNetworkStore::Current() const noexcept {
    return std::atomic_load_explicit(&current_, std::memory_order_acquire);
}

bool RoadNetworkStore::Rollback() {
    std::lock_guard lock{publish_mutex_};
    if (!previous_) return false;
    auto active = std::atomic_exchange_explicit(
        &current_, previous_, std::memory_order_acq_rel);
    previous_ = std::move(active);
    return true;
}

}  // namespace routing
