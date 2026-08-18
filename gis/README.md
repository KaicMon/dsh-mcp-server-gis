# GIS Core

`gis_core` is the project-owned isolation layer around GDAL/OGR. It provides
geometry validation and coordinate transformation to MCP plugins without
exposing GDAL object pointers across the plugin ABI. Geographic coordinates
use conventional GIS order: x/y means longitude/latitude.

## Build

Install the native dependencies in WSL:

```bash
sudo apt update
sudo apt install -y libgdal-dev gdal-bin libgeos-dev
```

Configure and run the initial smoke test under the shared development build
directory:

```bash
cmake -S . -B build_dev/gis-debug -DBUILD_GIS=ON
cmake --build build_dev/gis-debug --target gis_core_smoke_test -j
ctest --test-dir build_dev/gis-debug -R gis_core_smoke --output-on-failure
```

Use `-DBUILD_GIS=OFF` when building the rest of the project on a machine that
does not have GDAL development files.

## Runtime configuration

```bash
export AMAP_API_KEY='...'
export MCP_ROUTING_NETWORK='/absolute/path/to/nanjing-v1.route'
export GIS_CACHE_BACKEND=memory       # memory or redis
export GIS_CACHE_TTL_SECONDS=300
export GIS_PROVIDER_MAX_ATTEMPTS=2
export GIS_PROVIDER_REQUESTS_PER_SECOND=10
export GIS_PROVIDER_BURST=10
```

Start the cross-process result service before enabling external GeoJSON:

```bash
build_dev/gis-full-debug/gis/results/gis_result_server 18096
export GIS_RESULT_SERVICE_URL=http://127.0.0.1:18096
export GIS_MAX_INLINE_BYTES=32768
export GIS_MAX_INLINE_POINTS=500
export GIS_RESULT_TTL_SECONDS=900
```

Small results stay inline. Large results contain `resultId` and `mapUrl`;
opening the URL renders the stored GeoJSON with Leaflet.
