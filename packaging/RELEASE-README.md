# Release guide

This repository is distributed as two independent artifacts:

| Artifact | What it contains | What it does not contain |
| --- | --- | --- |
| GitHub Release `mcp-server-gis-linux-x86_64.tar.gz` | `mcp_server`, GIS plugin `.so` files, default Tool Profile and a DSH configuration template | GDAL/GEOS system libraries, API keys, OSM extracts, `.route` networks and user data |
| npm `@kaicmon/dsh-mcp-server-gis` | DSH client module: GIS guidance, **MCP Tool panel** and **Map** tab | the C++ runtime, native libraries, map data and credentials |

The two packages are intentionally separate: npm installs the DSH user
interface, while the GitHub Release provides a Linux-native MCP Runtime.

## Platform support

The first binary release targets **Linux x86_64** and is built on Ubuntu 22.04.
Install compatible runtime libraries through your system package manager. On
Ubuntu/Debian:

```bash
sudo apt-get install libgdal30 libgeos-c1v5 libproj22 libcurl4
```

Package names vary by distribution. If the server fails to start, inspect the
missing shared library with `ldd <prefix>/bin/mcp_server` and install the
matching package. A source build is the portable fallback.

## Install the C++ runtime

Replace `VERSION` with a GitHub Release tag and choose any writable `PREFIX`.

```bash
export VERSION=vX.Y.Z
export PREFIX="$HOME/.local/mcp-server-gis"
mkdir -p "$PREFIX"

curl -fL -O "https://github.com/KaicMon/dsh-mcp-server-gis/releases/download/${VERSION}/mcp-server-gis-linux-x86_64.tar.gz"
curl -fL -O "https://github.com/KaicMon/dsh-mcp-server-gis/releases/download/${VERSION}/mcp-server-gis-linux-x86_64.tar.gz.sha256"
sha256sum -c mcp-server-gis-linux-x86_64.tar.gz.sha256
tar -xzf mcp-server-gis-linux-x86_64.tar.gz -C "$PREFIX"
```

The installed layout is:

```text
PREFIX/
  bin/mcp_server
  lib/mcp-server-gis/plugins/{gis-analysis,map-services,osm-routing}/*.so
  share/mcp-server-gis/config/tool-profiles.json
  share/mcp-server-gis/dsh/gis-mcp.cordis.patch.example.yml
```

Verify plugin discovery without any network key or city data:

```bash
bash packaging/verify-install.sh --prefix "$PREFIX"
```

## Connect it to DeepSeek Harness

First install the published DSH client module into the Web profile:

```bash
dsh plugin --profile web add @kaicmon/dsh-mcp-server-gis
```

Then copy the runtime template from
`$PREFIX/share/mcp-server-gis/dsh/gis-mcp.cordis.patch.example.yml` into your
DSH profile patch file and replace every `__MCP_PREFIX__` with the absolute
value of `PREFIX`. It registers both:

- `@kaicmon/dsh-mcp-server-gis`: the Tool panel and Map tab;
- `@deepseek-ai/dsh-mcp-client`: the STDIO MCP client that launches
  `mcp_server`.

Keep optional values in the shell or service environment, never in the patch:

```bash
export AMAP_API_KEY='your-key'                     # online geocode / POI / route
export MCP_ROUTING_NETWORK='/data/nanjing.route'   # local OSM routing
export GIS_DATASET_ROOT='/data/gis-datasets'       # local GeoJSON datasets
```

Restart DSH after changing the profile. Without those variables, local GIS
analysis remains available; tools needing the corresponding provider or local
dataset return a normal configuration error.

## Build from source

Use this if your platform does not match the release binary:

```bash
sudo apt-get install build-essential cmake pkg-config \
  libcurl4-openssl-dev libgdal-dev libbz2-dev libexpat1-dev zlib1g-dev

cmake -S . -B build_dev/release \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_MCP_EXAMPLES=OFF \
  -DBUILD_GIS=ON -DBUILD_ROUTING=ON -DBUILD_TESTING=ON
cmake --build build_dev/release -j2
ctest --test-dir build_dev/release --output-on-failure
bash packaging/install-local.sh --build-dir build_dev/release --prefix "$HOME/.local/mcp-server-gis"
```

## Optional map data

City routing networks and GeoJSON datasets are separate downloads because they
have independent licenses, provenance and potentially large size. Each data
release should state its OSM extract date, builder version, checksum and
license. Do not upload a personal API key, PostGIS dump, or raw user location
history as a project Release asset.

## Maintainer release checklist

1. Run tests, then tag a clean source commit as `vX.Y.Z`.
2. Let the GitHub workflow build the archive, run installed-layout verification,
   and attach both the archive and SHA-256 file.
3. Confirm the npm package version remains a UI-only package; it must not gain
   C++ binaries, GDAL, keys or city data.
4. Before the first public source push, remove legacy tracked `build/` output
   from Git history/index. `.gitattributes` already keeps it out of generated
   source archives, but it should not remain in the repository tree.
