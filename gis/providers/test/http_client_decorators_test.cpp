#include "gis/cache/memory_response_cache.h"
#include "gis/providers/http_client.h"

#include <iostream>
#include <memory>
#include <vector>

namespace {

class SequenceClient final : public gis::providers::HttpClient {
public:
    explicit SequenceClient(std::vector<gis::providers::HttpResponse> responses)
        : responses_(std::move(responses)) {}
    gis::providers::HttpResponse Get(const std::string&,
        const gis::providers::QueryParameters&,
        std::chrono::milliseconds) override {
        ++calls;
        if (responses_.size() == 1) return responses_.front();
        auto value = responses_.front();
        responses_.erase(responses_.begin());
        return value;
    }
    int calls{0};
private:
    std::vector<gis::providers::HttpResponse> responses_;
};

bool Expect(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}

}  // namespace

int main() {
    using namespace gis::providers;
    bool ok = true;
    auto origin = std::make_shared<SequenceClient>(
        std::vector<HttpResponse>{{.status_code = 200, .body = "payload"}});
    CachingHttpClient cached(origin,
        std::make_shared<gis::cache::MemoryResponseCache>(),
        std::chrono::seconds(30));
    const QueryParameters first{{"b", " TWO "}, {"a", "ONE"}};
    const QueryParameters equivalent{{"A", "one"}, {"B", "two"}};
    ok &= Expect(!cached.Get("endpoint", first, std::chrono::seconds(1)).cache_hit,
                 "first request should miss cache");
    const auto hit = cached.Get("endpoint", equivalent, std::chrono::seconds(1));
    ok &= Expect(hit.cache_hit && hit.body == "payload" && origin->calls == 1,
                 "normalized equivalent request should hit cache");

    auto flaky = std::make_shared<SequenceClient>(std::vector<HttpResponse>{
        {.status_code = 503}, {.status_code = 200, .body = "ok"}});
    int sleeps = 0;
    RetryingHttpClient retrying(flaky, 2, std::chrono::milliseconds(1),
                                [&](auto) { ++sleeps; });
    const auto recovered = retrying.Get("endpoint", {}, std::chrono::seconds(1));
    ok &= Expect(recovered.status_code == 200 && recovered.attempts == 2 && sleeps == 1,
                 "retryable HTTP failure should retry once");

    auto limited_origin = std::make_shared<SequenceClient>(
        std::vector<HttpResponse>{{.status_code = 200}});
    RateLimitedHttpClient limited(limited_origin, 0.01, 1.0);
    ok &= Expect(limited.Get("endpoint", {}, std::chrono::seconds(1)).status_code == 200,
                 "initial burst token should pass");
    ok &= Expect(limited.Get("endpoint", {}, std::chrono::seconds(1)).status_code == 429 &&
                     limited_origin->calls == 1,
                 "exhausted bucket should fail without calling origin");
    return ok ? 0 : 1;
}
