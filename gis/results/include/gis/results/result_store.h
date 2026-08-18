#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace gis::results {

using Clock = std::chrono::system_clock;

struct StoredResult {
    std::string id;
    std::string payload;
    std::string content_type{"application/geo+json"};
    Clock::time_point created_at;
    Clock::time_point expires_at;
};

enum class StoreErrorCode {
    None,
    EmptyPayload,
    ResultTooLarge,
    InvalidTtl,
    NotFound,
    Expired,
    CapacityExceeded,
    Internal,
};

struct StoreError {
    StoreErrorCode code{StoreErrorCode::None};
    std::string message;
};

template <typename T>
struct StoreResult {
    std::optional<T> value;
    StoreError error;

    [[nodiscard]] bool Ok() const noexcept { return value.has_value(); }
};

struct PutResultRequest {
    std::string payload;
    std::string content_type{"application/geo+json"};
    std::chrono::seconds ttl{0};  // zero selects the store default
};

class ResultStore {
public:
    virtual ~ResultStore() = default;

    [[nodiscard]] virtual StoreResult<StoredResult> Put(PutResultRequest request) = 0;
    [[nodiscard]] virtual StoreResult<StoredResult> Get(const std::string& id) = 0;
    virtual bool Erase(const std::string& id) = 0;
};

}  // namespace gis::results
