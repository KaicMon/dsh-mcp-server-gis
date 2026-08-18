#include "gis/cache/redis_response_cache.h"

#include <hiredis/hiredis.h>

#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace gis::cache {
namespace {

using Context = std::unique_ptr<redisContext, decltype(&redisFree)>;
using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

Context Connect(const RedisCacheConfig& config) {
    const timeval timeout{static_cast<long>(config.timeout.count() / 1000),
                          static_cast<long>((config.timeout.count() % 1000) * 1000)};
    Context context(redisConnectWithTimeout(config.host.c_str(), config.port, timeout),
                    redisFree);
    if (context && context->err == 0) redisSetTimeout(context.get(), timeout);
    return context;
}

}  // namespace

RedisResponseCache::RedisResponseCache(RedisCacheConfig config)
    : config_(std::move(config)) {
    if (config_.host.empty() || config_.port <= 0 || config_.port > 65535 ||
        config_.timeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Invalid Redis cache configuration");
    }
}

std::optional<std::string> RedisResponseCache::Get(const std::string& key) {
    auto context = Connect(config_);
    if (!context || context->err) return std::nullopt;
    const std::string full_key = config_.key_prefix + key;
    Reply reply(static_cast<redisReply*>(redisCommand(
        context.get(), "GET %b", full_key.data(), full_key.size())), freeReplyObject);
    if (!reply || reply->type != REDIS_REPLY_STRING) return std::nullopt;
    return std::string(reply->str, static_cast<std::size_t>(reply->len));
}

bool RedisResponseCache::Put(const std::string& key, const std::string& value,
                             std::chrono::seconds ttl) {
    if (key.empty() || ttl <= std::chrono::seconds::zero()) return false;
    auto context = Connect(config_);
    if (!context || context->err) return false;
    const std::string full_key = config_.key_prefix + key;
    Reply reply(static_cast<redisReply*>(redisCommand(context.get(),
        "SET %b %b EX %lld", full_key.data(), full_key.size(), value.data(),
        value.size(), static_cast<long long>(ttl.count()))), freeReplyObject);
    return reply && reply->type == REDIS_REPLY_STATUS &&
           std::string_view(reply->str, reply->len) == "OK";
}

}  // namespace gis::cache
