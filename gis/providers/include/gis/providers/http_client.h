#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <vector>

namespace gis::providers {

using QueryParameters = std::vector<std::pair<std::string, std::string>>;

struct HttpResponse {
    long status_code{0};
    std::string body;
    std::string error;
    bool cache_hit{false};
    std::size_t attempts{1};

    [[nodiscard]] bool TransportOk() const noexcept { return error.empty(); }
};

class HttpClient {
public:
    virtual ~HttpClient() = default;

    [[nodiscard]] virtual HttpResponse Get(
        const std::string& url,
        const QueryParameters& parameters,
        std::chrono::milliseconds timeout) = 0;
};

class CurlHttpClient final : public HttpClient {
public:
    [[nodiscard]] HttpResponse Get(
        const std::string& url,
        const QueryParameters& parameters,
        std::chrono::milliseconds timeout) override;
};

}  // namespace gis::providers

namespace gis::cache { class ResponseCache; }

namespace gis::providers {

class CachingHttpClient final : public HttpClient {
public:
    CachingHttpClient(std::shared_ptr<HttpClient> inner,
                      std::shared_ptr<gis::cache::ResponseCache> cache,
                      std::chrono::seconds ttl);
    [[nodiscard]] HttpResponse Get(const std::string& url,
        const QueryParameters& parameters,
        std::chrono::milliseconds timeout) override;
private:
    std::shared_ptr<HttpClient> inner_;
    std::shared_ptr<gis::cache::ResponseCache> cache_;
    std::chrono::seconds ttl_;
};

class RetryingHttpClient final : public HttpClient {
public:
    using Sleeper = std::function<void(std::chrono::milliseconds)>;
    RetryingHttpClient(std::shared_ptr<HttpClient> inner,
        std::size_t max_attempts = 2,
        std::chrono::milliseconds initial_backoff = std::chrono::milliseconds(50),
        Sleeper sleeper = [](auto delay) { std::this_thread::sleep_for(delay); });
    [[nodiscard]] HttpResponse Get(const std::string& url,
        const QueryParameters& parameters,
        std::chrono::milliseconds timeout) override;
private:
    std::shared_ptr<HttpClient> inner_;
    std::size_t max_attempts_;
    std::chrono::milliseconds initial_backoff_;
    Sleeper sleeper_;
};

class RateLimitedHttpClient final : public HttpClient {
public:
    RateLimitedHttpClient(std::shared_ptr<HttpClient> inner,
                          double requests_per_second,
                          double burst_capacity);
    [[nodiscard]] HttpResponse Get(const std::string& url,
        const QueryParameters& parameters,
        std::chrono::milliseconds timeout) override;
private:
    std::shared_ptr<HttpClient> inner_;
    double rate_;
    double capacity_;
    double tokens_;
    std::chrono::steady_clock::time_point updated_at_;
    std::mutex mutex_;
};

}  // namespace gis::providers
