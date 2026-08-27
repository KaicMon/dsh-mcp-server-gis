# Open-source packaging plan

## Goal

Publish this repository as a self-hostable C++ GIS MCP Runtime with an
optional DeepSeek Harness integration. The release must not bundle credentials,
city-scale OSM data, or user-specific DSH profiles.

## Distribution boundary

```text
GitHub Release runtime archive
  ├─ bin/mcp_server
  ├─ lib/mcp-server-gis/plugins/*.so
  ├─ share/mcp-server-gis/config/tool-profiles.json
  └─ share/mcp-server-gis/dsh/gis-mcp.cordis.patch.example.yml

Optional downloads
  ├─ city-specific .route network
  └─ local GeoJSON datasets

Published DSH package
  └─ `@kaicmon/dsh-mcp-server-gis`: GIS map / Tool panel client module and
     GIS Agent guidance

Optional Host extensions (later)
  ├─ MCP control-plane
  └─ Tool catalog / RAG Bridge
```

## Current implementation

- CMake install rules package the Server, GIS plugins, tool profile, and DSH
  configuration template under one deterministic prefix.
- `packaging/install-local.sh` installs a verified local build without sudo.
- `packaging/verify-install.sh` starts the installed binary and verifies
  `initialize` plus GIS Tool discovery from the installed plugin directory.
- The template launches the runtime through the official
  `@deepseek-ai/dsh-mcp-client` over STDIO.
- Runtime dependencies such as GDAL, GEOS, PROJ and compatible glibc remain
  platform prerequisites; they are not hidden in an npm package.

## Release phases

1. **Runtime archive:** GitHub Actions builds Linux x86_64 release artifacts,
   runs focused CTest suites, emits SHA-256 checksums and a source archive.
2. **DSH npm package:** the Renderer is published as
   `@kaicmon/dsh-mcp-server-gis`; it exposes the map tab and Tool panel through
   DSH client-module discovery. Control-plane and RAG Bridge remain optional
   Host extensions while their APIs are still evolving.
3. **Installer:** download a versioned runtime archive by explicit release URL,
   verify its checksum, then print—not silently edit—the DSH patch required by
   the user profile.
4. **Optional data packs:** publish city `.route` files separately with source
   extract date, build version, checksum and license metadata.

## Security and release rules

- `AMAP_API_KEY`, DashScope keys, database URLs and local paths are environment
  inputs only; no release artifact contains them.
- The default DSH Tool profile is least-privilege and users explicitly choose
  local routing data and dataset roots.
- Do not claim Streamable HTTP as a released feature until its transport has a
  complete implementation and automated interoperability test.
- The repository uses MIT. Retain file-level notices for copied MIT source and
  review `NOTICE` before publishing a binary release.

## Package identity

- GitHub repository: `https://github.com/KaicMon/dsh-mcp-server-gis`
- Published npm package: `@kaicmon/dsh-mcp-server-gis`
- The scope belongs to the npm account named `kaicmon` (or to an organization
  created with that name); it is not derived automatically from GitHub.
