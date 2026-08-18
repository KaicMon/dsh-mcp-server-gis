#include "gis/results/geojson_output_policy.h"

#include <iostream>
#include <memory>

namespace {

class FakePublisher final : public gis::results::ResultPublisher {
public:
    gis::results::PublishResult Publish(
        const std::string&, const std::string&, std::chrono::seconds) override {
        ++calls;
        return {.value = gis::results::PublishedResult{
                    .result_id = "gis_0123456789abcdef0123456789abcdef",
                    .map_url = "http://127.0.0.1:8088/results/gis_test"}};
    }
    int calls{0};
};

bool Expect(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}

}  // namespace

int main() {
    auto publisher = std::make_shared<FakePublisher>();
    gis::results::GeoJsonOutputPolicy policy(
        {.max_inline_bytes = 16, .max_inline_points = 2,
         .ttl = std::chrono::seconds(30)}, publisher);
    bool ok = true;
    const auto small = policy.Prepare("{\"type\":\"Point\"}", 1);
    // The sample is 16+ bytes, so use an intentionally smaller representation
    // to prove both conditions must stay within their limits.
    const auto truly_small = policy.Prepare("{}", 1);
    ok &= Expect(truly_small.Ok() && truly_small.value->inline_result,
                 "small GeoJSON should remain inline");
    const auto bytes_large = policy.Prepare("{\"coordinates\":[1,2,3]}", 1);
    ok &= Expect(bytes_large.Ok() && !bytes_large.value->inline_result,
                 "byte threshold should publish externally");
    const auto points_large = policy.Prepare("{}", 3);
    ok &= Expect(points_large.Ok() && !points_large.value->inline_result,
                 "point threshold should publish externally");
    ok &= Expect(publisher->calls == 2,
                 "only external results should call the publisher");

    gis::results::GeoJsonOutputPolicy preview_policy(
        {.max_inline_bytes = 16, .max_inline_points = 2,
         .ttl = std::chrono::seconds(30), .publish_inline_results = true}, publisher);
    const auto preview = preview_policy.Prepare("{}", 1);
    ok &= Expect(preview.Ok() && preview.value->inline_result &&
                     !preview.value->result_id.empty() && !preview.value->map_url.empty(),
                 "published inline result should retain GeoJSON and expose a map URL");
    ok &= Expect(publisher->calls == 3,
                 "inline preview mode should publish one additional copy");
    (void)small;
    return ok ? 0 : 1;
}
