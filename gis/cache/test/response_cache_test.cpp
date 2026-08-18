#include "gis/cache/memory_response_cache.h"

#include <iostream>

namespace {
bool Expect(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
}

int main() {
    using namespace gis::cache;
    bool ok = true;
    const auto first = BuildCacheKey("Geocode",
        {{" City ", " NANJING "}, {"query", " University "}});
    const auto second = BuildCacheKey(" geocode ",
        {{"QUERY", "university"}, {"city", "nanjing"}});
    ok &= Expect(first == second && first.size() == 64,
                 "cache key must normalize order, case, and outer whitespace");

    auto now = MemoryResponseCache::Clock::time_point{};
    MemoryResponseCache cache(2, [&] { return now; });
    ok &= Expect(cache.Put(first, "value", std::chrono::seconds(5)),
                 "cache put should succeed");
    ok &= Expect(cache.Get(first) == "value", "cache hit should return payload");
    now += std::chrono::seconds(6);
    ok &= Expect(!cache.Get(first), "expired value must be a cache miss");
    return ok ? 0 : 1;
}
