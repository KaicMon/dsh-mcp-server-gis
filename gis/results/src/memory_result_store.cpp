#include "gis/results/memory_result_store.h"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gis::results {

MemoryResultStore::MemoryResultStore(MemoryResultStoreConfig config,
                                     NowFunction now)
    : config_(config), now_(std::move(now)) {
    if (config_.max_results == 0 || config_.max_total_bytes == 0 ||
        config_.max_result_bytes == 0 ||
        config_.max_result_bytes > config_.max_total_bytes ||
        config_.default_ttl <= std::chrono::seconds::zero() ||
        config_.max_ttl < config_.default_ttl || !now_) {
        throw std::invalid_argument("Invalid MemoryResultStore configuration");
    }

    // Seed with multiple random_device samples rather than timestamps or a
    // counter, so public result IDs cannot be trivially enumerated.
    std::random_device device;
    std::array<std::uint32_t, 8> seed_data{};
    for (auto& value : seed_data) value = device();
    std::seed_seq seed(seed_data.begin(), seed_data.end());
    random_.seed(seed);
}

StoreResult<StoredResult> MemoryResultStore::Put(PutResultRequest request) {
    if (request.payload.empty()) {
        return {.value = std::nullopt,
                .error = {StoreErrorCode::EmptyPayload, "Result payload must not be empty"}};
    }
    if (request.payload.size() > config_.max_result_bytes) {
        return {.value = std::nullopt,
                .error = {StoreErrorCode::ResultTooLarge,
                          "Result payload exceeds the per-result limit"}};
    }

    const auto ttl = request.ttl == std::chrono::seconds::zero()
        ? config_.default_ttl : request.ttl;
    if (ttl <= std::chrono::seconds::zero() || ttl > config_.max_ttl) {
        return {.value = std::nullopt,
                .error = {StoreErrorCode::InvalidTtl,
                          "Result TTL is outside the configured range"}};
    }

    std::lock_guard lock(mutex_);
    const auto now = now_();
    RemoveExpiredLocked(now);
    EvictForLocked(request.payload.size());
    if (entries_.size() >= config_.max_results ||
        total_bytes_ + request.payload.size() > config_.max_total_bytes) {
        return {.value = std::nullopt,
                .error = {StoreErrorCode::CapacityExceeded,
                          "Result store cannot accept the payload"}};
    }

    StoredResult stored{
        .id = GenerateIdLocked(),
        .payload = std::move(request.payload),
        .content_type = std::move(request.content_type),
        .created_at = now,
        .expires_at = now + ttl,
    };
    lru_.push_front(stored.id);
    total_bytes_ += stored.payload.size();
    entries_.emplace(stored.id, Entry{stored, lru_.begin()});
    return {.value = std::move(stored), .error = {}};
}

StoreResult<StoredResult> MemoryResultStore::Get(const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end()) {
        return {.value = std::nullopt,
                .error = {StoreErrorCode::NotFound, "Result was not found"}};
    }
    if (found->second.result.expires_at <= now_()) {
        RemoveLocked(found);
        return {.value = std::nullopt,
                .error = {StoreErrorCode::Expired, "Result has expired"}};
    }
    TouchLocked(found->second);
    return {.value = found->second.result, .error = {}};
}

bool MemoryResultStore::Erase(const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    RemoveLocked(found);
    return true;
}

std::size_t MemoryResultStore::Size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

std::size_t MemoryResultStore::TotalBytes() const {
    std::lock_guard lock(mutex_);
    return total_bytes_;
}

std::string MemoryResultStore::GenerateIdLocked() {
    for (;;) {
        std::ostringstream id;
        id << "gis_" << std::hex << std::setfill('0')
           << std::setw(16) << random_() << std::setw(16) << random_();
        if (!entries_.contains(id.str())) return id.str();
    }
}

void MemoryResultStore::RemoveLocked(
    std::unordered_map<std::string, Entry>::iterator entry) {
    total_bytes_ -= entry->second.result.payload.size();
    lru_.erase(entry->second.lru);
    entries_.erase(entry);
}

void MemoryResultStore::RemoveExpiredLocked(Clock::time_point now) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        if (entry->second.result.expires_at <= now) {
            auto expired = entry++;
            RemoveLocked(expired);
        } else {
            ++entry;
        }
    }
}

void MemoryResultStore::EvictForLocked(std::size_t incoming_bytes) {
    while (!lru_.empty() &&
           (entries_.size() >= config_.max_results ||
            total_bytes_ + incoming_bytes > config_.max_total_bytes)) {
        const auto found = entries_.find(lru_.back());
        if (found == entries_.end()) {
            lru_.pop_back();
        } else {
            RemoveLocked(found);
        }
    }
}

void MemoryResultStore::TouchLocked(Entry& entry) {
    lru_.splice(lru_.begin(), lru_, entry.lru);
    entry.lru = lru_.begin();
}

}  // namespace gis::results
