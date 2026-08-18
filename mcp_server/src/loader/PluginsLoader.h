//  The MIT License
//
//  Copyright (C) 2025 Giuseppe Mastrangelo

#ifndef MCP_SERVER_PLUGINS_LOADER_H
#define MCP_SERVER_PLUGINS_LOADER_H

#ifdef _WIN32
#include <windows.h>
using LibraryHandle = HMODULE;
#else
#include <dlfcn.h>
using LibraryHandle = void*;
#endif

#include <memory>
#include <atomic>
#include <string>
#include <vector>

#include "aixlog.hpp"
#include "PluginAPI.h"

namespace vx::mcp {

using CreatePluginFunc = PluginAPI* (*)();
using DestroyPluginFunc = void (*)(PluginAPI*);

class LoadedPlugin final {
public:
    static std::shared_ptr<LoadedPlugin> Load(
        const std::string& path,
        const PluginHostAPI& host_api);

    ~LoadedPlugin();

    LoadedPlugin(const LoadedPlugin&) = delete;
    LoadedPlugin& operator=(const LoadedPlugin&) = delete;
    LoadedPlugin(LoadedPlugin&&) = delete;
    LoadedPlugin& operator=(LoadedPlugin&&) = delete;

    PluginAPI& Api() const { return *instance_; }
    const std::string& Path() const { return path_; }
    void FreeResult(char* result) const;
    void DeactivateNotifications() noexcept;

private:
    LoadedPlugin(std::string path, PluginHostAPI host_api);
    bool Open();
    bool ResolveSymbols();
    bool CreateAndInitialize();
    bool ValidateAPI() const;
    void Unload() noexcept;
    static void ForwardNotification(void* context,
                                    const char* plugin_name,
                                    const char* notification);

    struct NotificationContext {
        std::atomic<bool> active{true};
        void* upstream_context = nullptr;
        ClientNotificationCallback upstream_callback = nullptr;
    };

    std::string path_;
    LibraryHandle handle_ = nullptr;
    PluginAPI* instance_ = nullptr;
    CreatePluginFunc create_func_ = nullptr;
    DestroyPluginFunc destroy_func_ = nullptr;
    PluginHostAPI host_api_{};
    NotificationContext notification_context_{};
    bool initialized_ = false;
};

class PluginsLoader {
public:
    PluginsLoader();
    ~PluginsLoader();

    void SetHostAPI(const PluginHostAPI& host_api);
    bool LoadPlugins(const std::string& directory);
    void UnloadPlugins();

    std::vector<std::shared_ptr<LoadedPlugin>> SnapshotPlugins() const;

private:
    bool LoadPlugin(const std::string& path);

    PluginHostAPI host_api_{};
    std::vector<std::shared_ptr<LoadedPlugin>> plugins_;
};

} // namespace vx::mcp

#endif // MCP_SERVER_PLUGINS_LOADER_H
