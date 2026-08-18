#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace gis::results {

struct PublishedResult {
    std::string result_id;
    std::string map_url;
};

struct PublishResult {
    std::optional<PublishedResult> value;
    std::string error;

    [[nodiscard]] bool Ok() const noexcept { return value.has_value(); }
};

// Process-boundary abstraction used by MCP plugins. The HTTP implementation
// publishes to gis_result_server; tests can inject a deterministic fake.
class ResultPublisher {
public:
    virtual ~ResultPublisher() = default;
    [[nodiscard]] virtual PublishResult Publish(
        const std::string& payload, const std::string& content_type,
        std::chrono::seconds ttl) = 0;
};

class HttpResultPublisher final : public ResultPublisher {
public:
    explicit HttpResultPublisher(std::string service_url,
                                 std::chrono::milliseconds timeout =
                                     std::chrono::milliseconds(3000));

    [[nodiscard]] PublishResult Publish(
        const std::string& payload, const std::string& content_type,
        std::chrono::seconds ttl) override;

private:
    std::string service_url_;
    std::chrono::milliseconds timeout_;
};

}  // namespace gis::results
