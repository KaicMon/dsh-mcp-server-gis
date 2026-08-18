#include "gis/providers/http_client.h"
#include "gis/cache/response_cache.h"

#include <curl/curl.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace gis::providers {
namespace {

void EnsureCurlInitialized() {
    static std::once_flag initialized;
    std::call_once(initialized, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct CurlDeleter {
    void operator()(CURL* curl) const noexcept { curl_easy_cleanup(curl); }
};

using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;

std::size_t WriteBody(char* data, std::size_t size, std::size_t count,
                      void* context) {
    const std::size_t bytes = size * count;
    static_cast<std::string*>(context)->append(data, bytes);
    return bytes;
}

std::string Escape(CURL* curl, const std::string& value) {
    char* escaped = curl_easy_escape(curl, value.c_str(),
                                     static_cast<int>(value.size()));
    if (escaped == nullptr) return {};
    std::string result(escaped);
    curl_free(escaped);
    return result;
}

}  // namespace

HttpResponse CurlHttpClient::Get(const std::string& url,
                                 const QueryParameters& parameters,
                                 std::chrono::milliseconds timeout) {
    EnsureCurlInitialized();
    CurlPtr curl(curl_easy_init());
    if (!curl) {
        return {.status_code = 0,
                .body = {},
                .error = "Unable to initialize libcurl"};
    }

    std::string request_url = url;
    char separator = request_url.find('?') == std::string::npos ? '?' : '&';
    for (const auto& [name, value] : parameters) {
        request_url.push_back(separator);
        separator = '&';
        request_url += Escape(curl.get(), name);
        request_url.push_back('=');
        request_url += Escape(curl.get(), value);
    }

    HttpResponse response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, request_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS,
                     static_cast<long>(timeout.count()));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(std::min(
                         timeout, std::chrono::milliseconds(3000)).count()));
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "mcp-gis-platform/0.1");

    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        response.error = curl_easy_strerror(code);
        return response;
    }
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
    return response;
}

CachingHttpClient::CachingHttpClient(
    std::shared_ptr<HttpClient> inner,
    std::shared_ptr<gis::cache::ResponseCache> cache,
    std::chrono::seconds ttl)
    : inner_(std::move(inner)), cache_(std::move(cache)), ttl_(ttl) {
    if (!inner_ || !cache_ || ttl_ <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("Invalid caching HTTP client configuration");
    }
}

HttpResponse CachingHttpClient::Get(const std::string& url,
                                    const QueryParameters& parameters,
                                    std::chrono::milliseconds timeout) {
    const std::string key = gis::cache::BuildCacheKey(url, parameters);
    if (auto cached = cache_->Get(key)) {
        return {.status_code = 200, .body = std::move(*cached), .error = {},
                .cache_hit = true, .attempts = 0};
    }
    auto response = inner_->Get(url, parameters, timeout);
    if (response.TransportOk() && response.status_code >= 200 &&
        response.status_code < 300) {
        cache_->Put(key, response.body, ttl_);
    }
    return response;
}

RetryingHttpClient::RetryingHttpClient(
    std::shared_ptr<HttpClient> inner, std::size_t max_attempts,
    std::chrono::milliseconds initial_backoff, Sleeper sleeper)
    : inner_(std::move(inner)), max_attempts_(max_attempts),
      initial_backoff_(initial_backoff), sleeper_(std::move(sleeper)) {
    if (!inner_ || max_attempts_ == 0 || initial_backoff_.count() < 0 || !sleeper_) {
        throw std::invalid_argument("Invalid retrying HTTP client configuration");
    }
}

HttpResponse RetryingHttpClient::Get(const std::string& url,
                                     const QueryParameters& parameters,
                                     std::chrono::milliseconds timeout) {
    HttpResponse response;
    for (std::size_t attempt = 1; attempt <= max_attempts_; ++attempt) {
        response = inner_->Get(url, parameters, timeout);
        response.attempts = attempt;
        const bool retryable = !response.TransportOk() || response.status_code == 429 ||
                               response.status_code >= 500;
        if (!retryable || attempt == max_attempts_) break;
        sleeper_(initial_backoff_ * static_cast<int>(attempt));
    }
    return response;
}

RateLimitedHttpClient::RateLimitedHttpClient(
    std::shared_ptr<HttpClient> inner, double requests_per_second,
    double burst_capacity)
    : inner_(std::move(inner)), rate_(requests_per_second),
      capacity_(burst_capacity), tokens_(burst_capacity),
      updated_at_(std::chrono::steady_clock::now()) {
    if (!inner_ || rate_ <= 0.0 || capacity_ < 1.0) {
        throw std::invalid_argument("Invalid rate-limited HTTP client configuration");
    }
}

HttpResponse RateLimitedHttpClient::Get(const std::string& url,
                                        const QueryParameters& parameters,
                                        std::chrono::milliseconds timeout) {
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        tokens_ = std::min(capacity_, tokens_ +
            std::chrono::duration<double>(now - updated_at_).count() * rate_);
        updated_at_ = now;
        if (tokens_ < 1.0) {
            return {.status_code = 429,
                    .body = {}, .error = {}, .cache_hit = false, .attempts = 0};
        }
        tokens_ -= 1.0;
    }
    return inner_->Get(url, parameters, timeout);
}

}  // namespace gis::providers
