#include "gis/results/memory_result_store.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gis::results;

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

}  // namespace

int main() {
    bool ok = true;
    auto now = Clock::time_point{};
    MemoryResultStore store(
        {.max_results = 2,
         .max_total_bytes = 12,
         .max_result_bytes = 8,
         .default_ttl = std::chrono::seconds(10),
         .max_ttl = std::chrono::seconds(30)},
        [&] { return now; });

    const auto first = store.Put({.payload = "first"});
    const auto second = store.Put({.payload = "second"});
    ok &= Expect(first.Ok() && second.Ok(), "valid payloads should be stored");
    ok &= Expect(first.value->id.starts_with("gis_") && first.value->id.size() == 36,
                 "result ID should use a 128-bit opaque suffix");
    ok &= Expect(store.Get(first.value->id).Ok(), "stored result should be retrievable");

    // Touching first makes second the least-recently-used entry.
    const auto third = store.Put({.payload = "third"});
    ok &= Expect(third.Ok(), "new entry should evict an older entry when full");
    ok &= Expect(store.Get(first.value->id).Ok(), "recently-read entry should remain");
    ok &= Expect(!store.Get(second.value->id).Ok(), "least-recently-used entry should be evicted");

    now += std::chrono::seconds(11);
    const auto expired = store.Get(first.value->id);
    ok &= Expect(!expired.Ok() && expired.error.code == StoreErrorCode::Expired,
                 "expired result should be removed with an explicit error");

    const auto too_large = store.Put({.payload = "123456789"});
    ok &= Expect(!too_large.Ok() && too_large.error.code == StoreErrorCode::ResultTooLarge,
                 "oversized result should be rejected");
    const auto bad_ttl = store.Put(
        {.payload = "ok", .ttl = std::chrono::seconds(31)});
    ok &= Expect(!bad_ttl.Ok() && bad_ttl.error.code == StoreErrorCode::InvalidTtl,
                 "TTL beyond the configured maximum should be rejected");

    MemoryResultStore concurrent;
    std::vector<std::thread> writers;
    for (int thread = 0; thread < 4; ++thread) {
        writers.emplace_back([&, thread] {
            for (int index = 0; index < 50; ++index) {
                const auto result = concurrent.Put(
                    {.payload = std::to_string(thread) + ":" + std::to_string(index)});
                if (!result.Ok()) std::terminate();
                if (!concurrent.Get(result.value->id).Ok()) std::terminate();
            }
        });
    }
    for (auto& writer : writers) writer.join();
    ok &= Expect(concurrent.Size() == 200,
                 "concurrent writers should not lose entries below capacity");

    return ok ? 0 : 1;
}
