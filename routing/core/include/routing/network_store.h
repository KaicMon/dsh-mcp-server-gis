#pragma once

#include "routing/router.h"
#include "routing/spatial_index.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace routing {

class RoadNetwork {
public:
    RoadNetwork(CsrGraph graph, std::string version);

    [[nodiscard]] const CsrGraph& Graph() const noexcept { return graph_; }
    [[nodiscard]] const Router& GetRouter() const noexcept { return *router_; }
    [[nodiscard]] const SpatialIndex& GetSpatialIndex() const noexcept { return *spatial_index_; }
    [[nodiscard]] const std::string& Version() const noexcept { return version_; }

private:
    CsrGraph graph_;
    std::string version_;
    std::unique_ptr<Router> router_;
    std::unique_ptr<SpatialIndex> spatial_index_;
};

class RoadNetworkStore {
public:
    using Snapshot = std::shared_ptr<const RoadNetwork>;

    // Staging performs all expensive parsing, checksum validation, and index
    // construction without changing the active generation.
    [[nodiscard]] Snapshot Stage(const std::filesystem::path& file,
                                 std::string version) const;
    void Publish(Snapshot candidate);
    [[nodiscard]] Snapshot Current() const noexcept;
    [[nodiscard]] bool Rollback();

private:
    // Access only through atomic shared_ptr free functions for GCC 11 support.
    Snapshot current_;
    std::mutex publish_mutex_;
    Snapshot previous_;
};

}  // namespace routing
