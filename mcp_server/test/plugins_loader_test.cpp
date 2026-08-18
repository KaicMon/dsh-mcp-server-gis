#include "PluginsLoader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void SetLifecycleLog(const std::filesystem::path& path) {
#ifdef _WIN32
    _putenv_s("MCP_TEST_LIFECYCLE_LOG", path.string().c_str());
#else
    setenv("MCP_TEST_LIFECYCLE_LOG", path.string().c_str(), 1);
#endif
}

std::vector<std::string> ReadEvents(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> events;
    std::string event;
    while (std::getline(input, event)) {
        events.push_back(event);
    }
    return events;
}

void ResetLog(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void TestValidPlugin(const std::filesystem::path& directory,
                     const std::filesystem::path& log_path) {
    ResetLog(log_path);
    std::shared_ptr<vx::mcp::LoadedPlugin> held_plugin;
    {
        vx::mcp::PluginsLoader loader;
        Check(loader.LoadPlugins(directory.string()),
              "valid plugin directory should be scanned successfully");
        auto plugins = loader.SnapshotPlugins();
        Check(plugins.size() == 1,
              "exactly one valid plugin should be loaded");

        if (plugins.size() == 1) {
            held_plugin = plugins.front();
            Check(held_plugin != nullptr, "valid plugin owner should be non-null");
            Check(std::string(held_plugin->Api().GetName()) == "loader-valid-plugin",
                  "loaded plugin name should match");
            Check(held_plugin->Api().GetToolCount() == 1,
                  "valid plugin should expose one tool");

            char* result = held_plugin->Api().HandleRequest("{}");
            Check(result != nullptr, "valid plugin should return an owned result");
            held_plugin->FreeResult(result);
        }

        loader.UnloadPlugins();
        plugins.clear();
        Check(loader.SnapshotPlugins().empty(),
              "UnloadPlugins should clear the loaded plugin list");
        Check(ReadEvents(log_path) == std::vector<std::string>({
                  "valid:create", "valid:initialize"}),
              "an external shared owner should defer plugin unload");
        loader.UnloadPlugins();
    }

    held_plugin.reset();

    const std::vector<std::string> expected{
        "valid:create",
        "valid:initialize",
        "valid:shutdown",
        "valid:destroy"
    };
    Check(ReadEvents(log_path) == expected,
          "valid plugin lifecycle should be create/initialize/shutdown/destroy exactly once");
}

void TestInitializationFailure(const std::filesystem::path& directory,
                               const std::filesystem::path& log_path) {
    ResetLog(log_path);
    vx::mcp::PluginsLoader loader;
    Check(loader.LoadPlugins(directory.string()),
          "directory scan currently succeeds even when a plugin fails initialization");
    Check(loader.SnapshotPlugins().empty(),
          "initialization failure must not publish the plugin");

    const std::vector<std::string> expected{
        "failure:create",
        "failure:initialize",
        "failure:destroy"
    };
    Check(ReadEvents(log_path) == expected,
          "failed initialization should destroy the candidate without publishing it");
}

void TestMissingExports(const std::filesystem::path& directory,
                        const std::filesystem::path& log_path) {
    ResetLog(log_path);
    vx::mcp::PluginsLoader loader;
    Check(loader.LoadPlugins(directory.string()),
          "directory scan currently succeeds when a library lacks plugin exports");
    Check(loader.SnapshotPlugins().empty(),
          "library without CreatePlugin/DestroyPlugin must not be published");
    Check(ReadEvents(log_path).empty(),
          "library without plugin exports must not produce lifecycle events");
}

void TestNullInstance(const std::filesystem::path& directory,
                      const std::filesystem::path& log_path) {
    ResetLog(log_path);
    vx::mcp::PluginsLoader loader;
    Check(loader.LoadPlugins(directory.string()),
          "directory scan should survive CreatePlugin returning nullptr");
    Check(loader.SnapshotPlugins().empty(),
          "null plugin instance must not be published");
}

void TestRejectedCandidate(const std::filesystem::path& directory,
                           const std::string& description) {
    vx::mcp::PluginsLoader loader;
    Check(loader.LoadPlugins(directory.string()),
          "directory scan should survive " + description);
    Check(loader.SnapshotPlugins().empty(),
          description + " must not be published");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 8) {
        std::cerr << "usage: plugins_loader_test <valid-dir> <failure-dir> "
                     "<missing-exports-dir> <null-instance-dir> "
                     "<incompatible-abi-dir> <small-api-dir> "
                     "<lifecycle-log>\n";
        return 2;
    }

    const std::filesystem::path log_path = argv[7];
    SetLifecycleLog(log_path);

    TestValidPlugin(argv[1], log_path);
    TestInitializationFailure(argv[2], log_path);
    TestMissingExports(argv[3], log_path);
    TestNullInstance(argv[4], log_path);
    TestRejectedCandidate(argv[5], "an incompatible ABI version");
    TestRejectedCandidate(argv[6], "an undersized PluginAPI structure");

    ResetLog(log_path);
    if (failures != 0) {
        std::cerr << failures << " PluginsLoader lifecycle assertion(s) failed\n";
        return 1;
    }

    std::cout << "PluginsLoader lifecycle baseline passed\n";
    return 0;
}
