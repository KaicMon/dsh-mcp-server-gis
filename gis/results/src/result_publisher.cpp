#include "gis/results/result_publisher.h"

#include "json.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

namespace gis::results {
namespace {

void EnsureCurlInitialized() {
    static std::once_flag initialized;
    std::call_once(initialized, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t AppendBody(char* data, std::size_t size, std::size_t count,
                       void* context) {
    const auto bytes = size * count;
    static_cast<std::string*>(context)->append(data, bytes);
    return bytes;
}

}  // namespace

HttpResultPublisher::HttpResultPublisher(std::string service_url,
                                         std::chrono::milliseconds timeout)
    : service_url_(std::move(service_url)), timeout_(timeout) {
    while (service_url_.ends_with('/')) service_url_.pop_back();
    if (service_url_.empty() || timeout_ <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Invalid result service configuration");
    }
}

PublishResult HttpResultPublisher::Publish(
    const std::string& payload, const std::string& content_type,
    std::chrono::seconds ttl) {
    EnsureCurlInitialized();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(
        curl_easy_init(), curl_easy_cleanup);
    if (!curl) return {.value = std::nullopt, .error = "Unable to initialize libcurl"};

    const std::string url = service_url_ + "/api/v1/results?ttlSeconds=" +
                            std::to_string(ttl.count());
    std::string response_body;
    curl_slist* raw_headers = nullptr;
    raw_headers = curl_slist_append(raw_headers,
        ("Content-Type: " + content_type).c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        raw_headers, curl_slist_free_all);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, payload.data());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(payload.size()));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, AppendBody);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS,
                     static_cast<long>(timeout_.count()));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(std::min(timeout_,
                         std::chrono::milliseconds(1000)).count()));
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);

    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        return {.value = std::nullopt, .error = curl_easy_strerror(code)};
    }
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status != 201) {
        return {.value = std::nullopt,
                .error = "Result service returned HTTP " + std::to_string(status)};
    }
    try {
        const auto body = json::parse(response_body);
        return {.value = PublishedResult{.result_id = body.at("resultId"),
                                        .map_url = body.at("mapUrl")},
                .error = {}};
    } catch (const std::exception& error) {
        return {.value = std::nullopt,
                .error = std::string{"Invalid result service response: "} + error.what()};
    }
}

}  // namespace gis::results
