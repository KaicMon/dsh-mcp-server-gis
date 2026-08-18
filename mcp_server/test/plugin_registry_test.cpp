#include "PluginRegistry.h"

#include <atomic>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::shared_ptr<const vx::mcp::PluginRegistry> LoadRegistry(
    const std::filesystem::path& directory,
    std::uint64_t generation,
    std::string* error,
    vx::mcp::PluginsLoader* loader) {
    if (!loader->LoadPlugins(directory.string())) {
        if (error) *error = "directory scan failed";
        return nullptr;
    }
    return vx::mcp::PluginRegistry::Build(
        loader->SnapshotPlugins(), generation, error);
}

void TestValidRegistry(const std::filesystem::path& directory) {
    vx::mcp::PluginsLoader loader;
    std::string error;
    auto registry = LoadRegistry(directory, 7, &error, &loader);
    Check(registry != nullptr, "valid metadata should build a registry: " + error);
    if (!registry) return;

    Check(registry->Generation() == 7, "registry should retain its generation");
    Check(registry->Tools().size() == 1, "registry should contain one tool");
    const auto* route = registry->FindTool("loader_test_tool");
    Check(route != nullptr, "tool route should be indexed by name");
    if (route) {
        Check(route->description == "Tool used by PluginsLoader lifecycle tests",
              "tool metadata should be copied into the host registry");
        Check(route->input_schema.is_object(),
              "tool JSON Schema should be parsed during staging");
    }
    Check(registry->FindTool("missing") == nullptr,
          "unknown tool lookup should fail");

    loader.UnloadPlugins();
    Check(route && std::string(route->plugin->Api().GetName()) ==
                       "loader-valid-plugin",
          "registry snapshot should keep the plugin loaded");
}

void TestInvalidSchema(const std::filesystem::path& directory) {
    vx::mcp::PluginsLoader loader;
    std::string error;
    auto registry = LoadRegistry(directory, 1, &error, &loader);
    Check(registry == nullptr, "invalid JSON Schema must reject staging");
    Check(error.find("invalid JSON") != std::string::npos,
          "invalid Schema rejection should explain the cause");
}

void TestDuplicateTool(const std::filesystem::path& directory) {
    vx::mcp::PluginsLoader loader;
    std::string error;
    auto registry = LoadRegistry(directory, 1, &error, &loader);
    Check(registry == nullptr, "duplicate tool names must reject staging");
    Check(error.find("Duplicate tool name") != std::string::npos,
          "duplicate rejection should identify the key type");
}

void TestAtomicPublication(const std::filesystem::path& directory) {
    vx::mcp::PluginsLoader loader;
    std::string error;
    auto first = LoadRegistry(directory, 1, &error, &loader);
    auto second = vx::mcp::PluginRegistry::Build(
        loader.SnapshotPlugins(), 2, &error);
    Check(first && second, "both registry generations should build");
    if (!first || !second) return;

    vx::mcp::PluginRegistryStore store(first);
    const auto old_snapshot = store.Snapshot();
    store.Publish(second);
    const auto new_snapshot = store.Snapshot();
    Check(old_snapshot->Generation() == 1,
          "held snapshot should remain on the old generation");
    Check(new_snapshot->Generation() == 2,
          "new snapshots should observe the published generation");

    std::atomic<bool> start{false};
    std::atomic<int> concurrent_failures{0};
    std::vector<std::thread> readers;
    for (int reader = 0; reader < 4; ++reader) {
        readers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {}
            for (int i = 0; i < 10000; ++i) {
                const auto snapshot = store.Snapshot();
                if (!snapshot ||
                    (snapshot->Generation() != 1 && snapshot->Generation() != 2) ||
                    !snapshot->FindTool("loader_test_tool")) {
                    ++concurrent_failures;
                }
            }
        });
    }
    std::thread publisher([&] {
        start.store(true, std::memory_order_release);
        for (int i = 0; i < 10000; ++i) {
            store.Publish((i % 2 == 0) ? first : second);
        }
    });
    publisher.join();
    for (auto& reader : readers) reader.join();
    Check(concurrent_failures.load() == 0,
          "concurrent readers should only observe complete registry snapshots");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: plugin_registry_test <valid-dir> "
                     "<invalid-schema-dir> <duplicate-tools-dir>\n";
        return 2;
    }

    TestValidRegistry(argv[1]);
    TestInvalidSchema(argv[2]);
    TestDuplicateTool(argv[3]);
    TestAtomicPublication(argv[1]);

    if (failures != 0) {
        std::cerr << failures << " PluginRegistry assertion(s) failed\n";
        return 1;
    }
    std::cout << "PluginRegistry staging and publication tests passed\n";
    return 0;
}
