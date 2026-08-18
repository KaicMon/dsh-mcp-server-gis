#ifndef MCP_SERVER_PLUGIN_WATCHER_H
#define MCP_SERVER_PLUGIN_WATCHER_H

#include <filesystem>
#include <functional>
#include <memory>

namespace vx::mcp {

struct PluginFileEvent {
    enum class Kind { CreatedOrModified, Removed, RescanRequired };
    Kind kind;
    std::filesystem::path path;
};

class IPluginWatcher {
public:
    using Callback = std::function<void(const PluginFileEvent&)>;
    virtual ~IPluginWatcher() = default;
    virtual bool Start(Callback callback) = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IPluginWatcher> CreatePluginWatcher(
    const std::filesystem::path& directory);

} // namespace vx::mcp

#endif // MCP_SERVER_PLUGIN_WATCHER_H
