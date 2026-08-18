#include "gis/cache/redis_response_cache.h"

#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    gis::cache::RedisResponseCache cache({.host = "127.0.0.1",
        .port = std::stoi(argv[1]), .key_prefix = "gis:test:",
        .timeout = std::chrono::milliseconds(500)});
    const std::string key = "binary-safe";
    const std::string payload{"a\0b", 3};
    if (!cache.Put(key, payload, std::chrono::seconds(10))) return 1;
    const auto found = cache.Get(key);
    if (!found || *found != payload) {
        std::cerr << "Redis cache round trip failed\n";
        return 1;
    }
    return 0;
}
