#include "gis/gdal_runtime.h"

#include <mutex>

#include <gdal_priv.h>

namespace gis {

void GdalRuntime::EnsureInitialized() {
    static std::once_flag initialized;
    std::call_once(initialized, [] { GDALAllRegister(); });
}

}  // namespace gis
