#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gis::cache {

class ResponseCache {
public:
    virtual ~ResponseCache() = default;
    [[nodiscard]] virtual std::optional<std::string> Get(
        const std::string& key) = 0;
    virtual bool Put(const std::string& key, const std::string& value,
                     std::chrono::seconds ttl) = 0;
};

// Canonicalizes operation parameters before hashing. Sorting and whitespace
// normalization prevent semantically identical requests from fragmenting the
// cache merely because JSON/object insertion order differs.
[[nodiscard]] std::string BuildCacheKey(
    const std::string& operation,
    const std::vector<std::pair<std::string, std::string>>& parameters);

}  // namespace gis::cache
