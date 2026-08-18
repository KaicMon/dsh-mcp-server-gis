#pragma once

#include "gis/results/result_store.h"

#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

namespace gis::results {

struct MemoryResultStoreConfig {
    std::size_t max_results{512};
    std::size_t max_total_bytes{64U * 1024U * 1024U};
    std::size_t max_result_bytes{8U * 1024U * 1024U};
    std::chrono::seconds default_ttl{std::chrono::minutes(15)};
    std::chrono::seconds max_ttl{std::chrono::hours(24)};
};

// Thread-safe bounded in-process result store. Entries are evicted by LRU
// after expired entries are removed; this prevents large map results from
// growing the MCP/HTTP process without limit.
class MemoryResultStore final : public ResultStore {
public:
    using NowFunction = std::function<Clock::time_point()>;

    explicit MemoryResultStore(MemoryResultStoreConfig config = {},
                               NowFunction now = [] { return Clock::now(); });

    [[nodiscard]] StoreResult<StoredResult> Put(PutResultRequest request) override;
    [[nodiscard]] StoreResult<StoredResult> Get(const std::string& id) override;
    bool Erase(const std::string& id) override;

    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] std::size_t TotalBytes() const;

private:
    struct Entry {
        StoredResult result;
        std::list<std::string>::iterator lru;
    };

    std::string GenerateIdLocked();
    void RemoveLocked(std::unordered_map<std::string, Entry>::iterator entry);
    void RemoveExpiredLocked(Clock::time_point now);
    void EvictForLocked(std::size_t incoming_bytes);
    void TouchLocked(Entry& entry);

    MemoryResultStoreConfig config_;
    NowFunction now_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::list<std::string> lru_;
    std::size_t total_bytes_{0};
    std::mt19937_64 random_;
};

}  // namespace gis::results
