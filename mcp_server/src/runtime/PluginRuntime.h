#ifndef MCP_SERVER_PLUGIN_RUNTIME_H
#define MCP_SERVER_PLUGIN_RUNTIME_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <condition_variable>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "PluginRegistry.h"
#include "PluginWatcher.h"

namespace vx::mcp {

struct PluginRuntimeConfig {
    std::filesystem::path source_directory;
    std::filesystem::path staging_directory;
    PluginHostAPI host_api{};
    std::size_t retain_generations = 2;
    std::chrono::milliseconds debounce{300};
    std::chrono::milliseconds delete_grace{500};
    std::function<void(const std::string& method)> capability_notification;
};

struct ReloadResult {
    bool success = false;
    bool changed = false;
    std::uint64_t generation = 0;
    std::string message;
};

struct PluginRuntimeStatus {
    std::uint64_t generation = 0;
    std::size_t active_plugins = 0;
    bool watching = false;
};

class PluginRuntime final {
public:
    PluginRuntime() = default;
    ~PluginRuntime();

    PluginRuntime(const PluginRuntime&) = delete;
    PluginRuntime& operator=(const PluginRuntime&) = delete;

    bool Initialize(const PluginRuntimeConfig& config, std::string* error = nullptr);
    void Shutdown();

    std::shared_ptr<const PluginRegistry> Snapshot() const;
    ReloadResult Reload(const std::filesystem::path& source_path);
    ReloadResult Remove(const std::filesystem::path& source_path);
    bool StartWatching(std::string* error = nullptr);
    void StopWatching();
    PluginRuntimeStatus Status() const;

private:
    struct ActivePlugin {
        std::filesystem::path source_path;
        std::filesystem::path staged_path;
        std::string sha256;
        std::string plugin_name;
        std::string plugin_version;
        std::uint64_t generation = 0;
        std::shared_ptr<LoadedPlugin> plugin;
    };

    ReloadResult ReloadLocked(const std::filesystem::path& source_path);
    std::shared_ptr<const PluginRegistry> BuildRegistry(
        const std::map<std::string, ActivePlugin>& plugins,
        std::uint64_t generation,
        std::string* error) const;
    std::filesystem::path Stage(
        const std::filesystem::path& source_path,
        const std::string& source_key,
        const std::string& sha256,
        std::uint64_t generation,
        std::string* error) const;
    void QueueFileEvent(const PluginFileEvent& event);
    void EventLoop();
    void Reconcile();
    void NotifyCapabilityChanges(
        const std::shared_ptr<const PluginRegistry>& previous,
        const std::shared_ptr<const PluginRegistry>& current) const;
    void CollectGarbage();
    void MarkMetadataValidated(const ActivePlugin& plugin) const;

    mutable std::mutex update_mutex_;
    PluginRuntimeConfig config_{};
    PluginRegistryStore registry_store_{nullptr};
    std::map<std::string, ActivePlugin> active_plugins_;
    std::uint64_t generation_ = 0;
    std::uint64_t staging_sequence_ = 0;
    std::uint64_t reload_sequence_ = 0;
    struct StagedGeneration {
        std::string source_key;
        std::filesystem::path directory;
        std::weak_ptr<LoadedPlugin> plugin;
    };
    std::vector<StagedGeneration> staged_history_;
    bool initialized_ = false;
    std::unique_ptr<IPluginWatcher> watcher_;
    mutable std::mutex event_mutex_;
    std::condition_variable event_cv_;
    struct PendingEvent {
        PluginFileEvent::Kind kind;
        std::chrono::steady_clock::time_point ready_at;
    };
    std::unordered_map<std::string, PendingEvent> pending_events_;
    std::thread event_thread_;
    bool event_running_ = false;
};

} // namespace vx::mcp

#endif // MCP_SERVER_PLUGIN_RUNTIME_H
