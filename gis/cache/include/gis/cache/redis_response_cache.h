#pragma once

#include "gis/cache/response_cache.h"

#include <string>

namespace gis::cache {

struct RedisCacheConfig {
    std::string host{"127.0.0.1"};
    int port{6379};
    std::string key_prefix{"gis:provider:v1:"};
    std::chrono::milliseconds timeout{500};
};

class RedisResponseCache final : public ResponseCache {
public:
    explicit RedisResponseCache(RedisCacheConfig config = {});
    [[nodiscard]] std::optional<std::string> Get(const std::string& key) override;
    bool Put(const std::string& key, const std::string& value,
             std::chrono::seconds ttl) override;

private:
    RedisCacheConfig config_;
};

}  // namespace gis::cache
