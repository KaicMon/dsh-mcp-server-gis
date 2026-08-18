#include "gis/cache/memory_response_cache.h"

#include <stdexcept>

namespace gis::cache {

MemoryResponseCache::MemoryResponseCache(std::size_t max_entries, NowFunction now)
    : max_entries_(max_entries), now_(std::move(now)) {
    if (max_entries_ == 0 || !now_) throw std::invalid_argument("Invalid cache configuration");
}

std::optional<std::string> MemoryResponseCache::Get(const std::string& key) {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    if (found == entries_.end()) return std::nullopt;
    if (found->second.expires_at <= now_()) {
        entries_.erase(found);
        return std::nullopt;
    }
    return found->second.value;
}

bool MemoryResponseCache::Put(const std::string& key, const std::string& value,
                              std::chrono::seconds ttl) {
    if (key.empty() || ttl <= std::chrono::seconds::zero()) return false;
    std::lock_guard lock(mutex_);
    if (!entries_.contains(key) && entries_.size() >= max_entries_) {
        auto oldest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.expires_at < oldest->second.expires_at) oldest = it;
        }
        entries_.erase(oldest);
    }
    entries_[key] = {value, now_() + ttl};
    return true;
}

}  // namespace gis::cache
