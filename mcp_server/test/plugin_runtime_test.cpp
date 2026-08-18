#include "PluginRuntime.h"

#include <filesystem>
#include <atomic>
#include <fstream>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
void Check(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

std::string Invoke(const std::shared_ptr<const vx::mcp::PluginRegistry>& registry) {
    const auto* route = registry->FindTool("runtime_tool");
    if (!route) return {};
    char* raw = route->plugin->Api().HandleRequest("{}");
    if (!raw) return {};
    std::string result(raw);
    route->plugin->FreeResult(raw);
    return result;
}

template<typename Predicate>
bool WaitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

void CountPluginNotification(void* context, const char*, const char*) {
    ++(*static_cast<std::atomic<int>*>(context));
}
}

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: plugin_runtime_test <v1> <v2> <bad> "
                     "<second-plugin> <work-dir>\n";
        return 2;
    }
    const std::filesystem::path work = argv[5];
    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
    const auto source_dir = work / "source";
    const auto staging_dir = work / "staging";
    std::filesystem::create_directories(source_dir);
    const auto deployed = source_dir / "runtime-plugin.so";
    std::filesystem::copy_file(argv[1], deployed);

    vx::mcp::PluginRuntime runtime;
    vx::mcp::PluginRuntimeConfig config;
    config.source_directory = source_dir;
    config.staging_directory = staging_dir;
    config.host_api.abiVersion = MCP_PLUGIN_ABI_VERSION;
    config.host_api.structSize = sizeof(PluginHostAPI);
    std::atomic<int> plugin_notifications{0};
    config.host_api.hostContext = &plugin_notifications;
    config.host_api.SendToClient = CountPluginNotification;
    std::atomic<int> capability_notifications{0};
    config.capability_notification = [&](const std::string& method) {
        if (method == "notifications/tools/list_changed") {
            ++capability_notifications;
        }
    };
    std::string error;
    Check(runtime.Initialize(config, &error), "runtime should initialize: " + error);
    auto old_snapshot = runtime.Snapshot();
    Check(old_snapshot && old_snapshot->Generation() == 1,
          "initial plugin should publish generation 1");
    Check(Invoke(old_snapshot).find("v1") != std::string::npos,
          "initial snapshot should invoke v1");
    Check(plugin_notifications == 1,
          "active generation notification should reach the host");
    capability_notifications = 0;

    std::filesystem::copy_file(argv[3], deployed,
        std::filesystem::copy_options::overwrite_existing);
    auto bad = runtime.Reload(deployed);
    Check(!bad.success && !bad.changed, "bad candidate must be rejected");
    Check(runtime.Snapshot()->Generation() == 1,
          "failed candidate must preserve current generation");
    Check(Invoke(runtime.Snapshot()).find("v1") != std::string::npos,
          "failed candidate must preserve v1 service");
    Check(capability_notifications == 0,
          "failed candidate must not emit capability notifications");

    std::filesystem::copy_file(argv[2], deployed,
        std::filesystem::copy_options::overwrite_existing);
    auto updated = runtime.Reload(deployed);
    Check(updated.success && updated.changed && updated.generation == 2,
          "valid v2 should publish generation 2: " + updated.message);
    Check(Invoke(runtime.Snapshot()).find("v2") != std::string::npos,
          "new snapshot should invoke v2");
    Check(Invoke(old_snapshot).find("v1") != std::string::npos,
          "held old snapshot should still invoke v1");
    Check(plugin_notifications == 2,
          "retired generation notification should be discarded");
    Check(capability_notifications == 0,
          "implementation-only update must not emit list_changed");

    auto unchanged = runtime.Reload(deployed);
    Check(unchanged.success && !unchanged.changed,
          "same SHA-256 should skip reload");

    auto removed = runtime.Remove(deployed);
    Check(removed.success && removed.changed && removed.generation == 3,
          "remove should publish generation 3");
    Check(runtime.Snapshot()->FindTool("runtime_tool") == nullptr,
          "removed tool must disappear from new snapshots");
    Check(capability_notifications == 1,
          "tool removal should emit one tools/list_changed notification");
    Check(Invoke(old_snapshot).find("v1") != std::string::npos,
          "old snapshot should survive remove");

    config.debounce = std::chrono::milliseconds(50);
    config.delete_grace = std::chrono::milliseconds(50);
    std::string watch_error;
    Check(runtime.StartWatching(&watch_error),
          "inotify watcher should start: " + watch_error);
    std::filesystem::copy_file(argv[1], deployed,
        std::filesystem::copy_options::overwrite_existing);
    Check(WaitFor([&] {
              return Invoke(runtime.Snapshot()).find("v1") != std::string::npos;
          }), "watcher should publish a newly created v1 plugin");
    Check(WaitFor([&] { return capability_notifications.load() == 2; }),
          "tool creation should emit tools/list_changed");
    const auto after_create = runtime.Snapshot()->Generation();
    std::filesystem::copy_file(argv[2], deployed,
        std::filesystem::copy_options::overwrite_existing);
    Check(WaitFor([&] {
              return Invoke(runtime.Snapshot()).find("v2") != std::string::npos;
          }), "watcher should publish modified v2 plugin");
    Check(runtime.Snapshot()->Generation() == after_create + 1,
          "debounced replacement should publish one effective generation");
    Check(capability_notifications == 2,
          "same public metadata should not emit another list_changed");
    std::filesystem::remove(deployed);
    Check(WaitFor([&] {
              return runtime.Snapshot()->FindTool("runtime_tool") == nullptr;
          }), "watcher should remove a deleted plugin after grace period");
    Check(WaitFor([&] { return capability_notifications.load() == 3; }),
          "watched removal should emit tools/list_changed");
    runtime.StopWatching();

    std::filesystem::copy_file(argv[1], deployed,
        std::filesystem::copy_options::overwrite_existing);
    Check(runtime.Reload(deployed).success,
          "stress baseline v1 should load");
    std::atomic<bool> stress_start{false};
    std::atomic<int> stress_failures{0};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            while (!stress_start.load(std::memory_order_acquire)) {}
            for (int call = 0; call < 5000; ++call) {
                const std::string value = Invoke(runtime.Snapshot());
                if (value.find("v1") == std::string::npos &&
                    value.find("v2") == std::string::npos) {
                    ++stress_failures;
                }
            }
        });
    }
    std::thread reloader([&] {
        stress_start.store(true, std::memory_order_release);
        for (int update = 0; update < 40; ++update) {
            const char* candidate = update % 2 == 0 ? argv[2] : argv[1];
            std::filesystem::copy_file(candidate, deployed,
                std::filesystem::copy_options::overwrite_existing);
            if (!runtime.Reload(deployed).success) ++stress_failures;
        }
    });
    reloader.join();
    for (auto& reader : readers) reader.join();
    Check(stress_failures == 0,
          "concurrent calls and repeated reloads should see only complete versions");

    const auto second_deployed = source_dir / "second-plugin.so";
    std::filesystem::copy_file(argv[4], second_deployed);
    std::filesystem::copy_file(argv[2], deployed,
        std::filesystem::copy_options::overwrite_existing);
    vx::mcp::ReloadResult first_update;
    vx::mcp::ReloadResult second_add;
    std::thread first_plugin_thread([&] { first_update = runtime.Reload(deployed); });
    std::thread second_plugin_thread([&] { second_add = runtime.Reload(second_deployed); });
    first_plugin_thread.join();
    second_plugin_thread.join();
    Check(first_update.success && second_add.success,
          "simultaneous multi-plugin updates should serialize successfully");
    Check(runtime.Snapshot()->FindTool("runtime_tool") &&
          runtime.Snapshot()->FindTool("loader_test_tool"),
          "multi-plugin Registry should contain both complete routes");
    Check(runtime.Remove(second_deployed).success,
          "second plugin should be removable after concurrent publication");

    std::size_t generation_directories = 0;
    std::size_t validated_metadata = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(staging_dir)) {
        if (entry.is_directory() &&
            entry.path().filename().string().starts_with("generation-")) {
            ++generation_directories;
        }
        if (entry.is_regular_file() && entry.path().filename() == "metadata.json") {
            std::ifstream input(entry.path());
            const std::string metadata((std::istreambuf_iterator<char>(input)), {});
            if (metadata.find("\"validated\": true") != std::string::npos) {
                ++validated_metadata;
            }
        }
    }
    Check(generation_directories <= 5,
          "GC should retain configured generations plus held old snapshots");
    Check(validated_metadata > 0,
          "published staging generations should have validated metadata");

    const auto benchmark_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) {
        if (!runtime.Snapshot()->FindTool("runtime_tool")) ++stress_failures;
    }
    const auto benchmark_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - benchmark_start).count();
    std::cout << "100000 atomic snapshots and tool lookups: "
              << benchmark_ms << "ms\n";
    Check(stress_failures == 0, "snapshot lookup benchmark should remain correct");

    old_snapshot.reset();
    std::thread shutdown_thread([&] { runtime.Shutdown(); });
    std::thread racing_reload([&] { (void)runtime.Reload(deployed); });
    shutdown_thread.join();
    racing_reload.join();
    Check(runtime.Snapshot() && runtime.Snapshot()->Tools().empty(),
          "shutdown racing reload should finish with an empty Registry");
    std::filesystem::remove_all(work, ignored);
    if (failures) return 1;
    std::cout << "PluginRuntime atomic reload tests passed\n";
    return 0;
}
