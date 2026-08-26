# Modern C++ runtime and routing upgrades

This document records modern C++ features that have a measurable role in the
MCP/GIS runtime. It intentionally excludes syntax-only changes.

## Completed

| Feature | Code location | Purpose |
| --- | --- | --- |
| Atomic `shared_ptr` Registry publication | `mcp_server/src/loader/PluginRegistry.{h,cpp}` | Publishes an immutable Plugin Registry with acquire/release semantics; held request snapshots keep old plugins alive through hot replacement. |
| Concurrent snapshot test | `mcp_server/test/plugin_registry_test.cpp` | Readers repeatedly load while a publisher swaps generations, proving no partial registry is observed. |
| `std::jthread` + `std::stop_token` | `mcp_server/src/runtime/PluginRuntime.{h,cpp}` | Owns and cancels the file-event debounce thread safely during watcher shutdown. |
| `std::jthread` + `std::stop_token` | `mcp_server/src/runtime/PluginWatcher.cpp` | Wakes and stops the blocking Linux inotify monitor through its wake pipe before joining. |
| `std::jthread` + `std::stop_token` | `mcp_server/src/server/Server.{h,cpp}` | Gives the asynchronous notification Writer an explicit cancellation channel and closes the transport-start failure thread leak. |
| `std::span` CSR views | `routing/core/include/routing/csr_graph.h` | Provides zero-copy target and edge-geometry ranges over immutable CSR arrays. |
| `std::pmr` query memory | `routing/core/src/router.cpp` | Groups Dijkstra/A* and bidirectional scratch allocations in one per-request monotonic arena. |

## Validation

Run the focused checks from a configured build directory:

```bash
cmake --build build_dev/gis-full-debug -j2 --target router_test csr_graph_test plugin_registry_test plugin_runtime_test
ctest --test-dir build_dev/gis-full-debug --output-on-failure -R 'routing_(router|csr_graph)|plugin_(registry|runtime)'
```

`routing/benchmark/route_benchmark.cpp` already records P50/P95/P99. Future
changes must compare equal graph, query seed, algorithm, build type, and
machine; no performance claim should be made before that comparison.

## Next, only after profiling

1. Propagate `std::stop_token` into cancellable long-running plugin work.
2. Measure `pmr` allocation count and tail latency on a city-scale route file.
3. Consider `std::counting_semaphore` or `atomic_wait` for high-rate
   notification queues only if contention appears in profiling.
4. Keep C++ coroutines out of the STDIO path until a real concurrent HTTP or
   database-I/O workload justifies their state-machine complexity.
