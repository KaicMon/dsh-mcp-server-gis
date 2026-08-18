#include "gis/providers/gcj02_coordinate_adapter.h"

#include <cmath>
#include <iostream>

namespace {

using namespace gis::providers;

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

}  // namespace

int main() {
    const Gcj02CoordinateAdapter adapter;
    bool ok = true;

    const Coordinate nanjing{118.7969, 32.0603, CoordinateSystem::Wgs84};
    const auto provider = adapter.ToProvider(nanjing);
    ok &= Expect(provider.has_value() &&
                     provider->system == CoordinateSystem::Gcj02,
                 "mainland WGS84 coordinate should convert to GCJ-02");
    ok &= Expect(provider && std::abs(provider->longitude - nanjing.longitude) > 0.001,
                 "mainland conversion should apply an observable offset");

    const auto restored = provider ? adapter.FromProvider(*provider) : std::nullopt;
    ok &= Expect(restored.has_value() &&
                     std::abs(restored->longitude - nanjing.longitude) < 1e-7 &&
                     std::abs(restored->latitude - nanjing.latitude) < 1e-7,
                 "iterative inverse should round-trip below API coordinate precision");

    const Coordinate london{-0.1276, 51.5072, CoordinateSystem::Wgs84};
    const auto london_provider = adapter.ToProvider(london);
    ok &= Expect(london_provider &&
                     london_provider->longitude == london.longitude &&
                     london_provider->latitude == london.latitude,
                 "coordinates outside mainland China should remain unchanged");

    const Coordinate wrong_label{118.8, 32.0, CoordinateSystem::Gcj02};
    ok &= Expect(!adapter.ToProvider(wrong_label),
                 "adapter must reject a coordinate with the wrong source label");
    return ok ? 0 : 1;
}
