#pragma once

#include "gis/cache/response_cache.h"

#include <functional>
#include <mutex>
#include <unordered_map>

namespace gis::cache {

class MemoryResponseCache final : public ResponseCache {
public:
    using Clock = std::chrono::steady_clock;
    using NowFunction = std::function<Clock::time_point()>;

    explicit MemoryResponseCache(std::size_t max_entries = 1024,
        NowFunction now = [] { return Clock::now(); });
    [[nodiscard]] std::optional<std::string> Get(const std::string& key) override;
    bool Put(const std::string& key, const std::string& value,
             std::chrono::seconds ttl) override;

private:
    struct Entry { std::string value; Clock::time_point expires_at; };
    std::size_t max_entries_;
    NowFunction now_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace gis::cache
