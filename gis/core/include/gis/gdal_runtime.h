#pragma once

namespace gis {

// Performs GDAL's process-wide registration exactly once. Public GIS entry
// points call this internally, so callers and plugins do not need to coordinate
// initialization order.
class GdalRuntime final {
public:
    static void EnsureInitialized();
};

}  // namespace gis
