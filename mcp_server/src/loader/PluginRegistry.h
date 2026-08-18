#ifndef MCP_SERVER_PLUGIN_REGISTRY_H
#define MCP_SERVER_PLUGIN_REGISTRY_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "PluginsLoader.h"
#include "json.hpp"

namespace vx::mcp {

struct ToolRoute {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    std::shared_ptr<LoadedPlugin> plugin;
};

struct PromptRoute {
    std::string name;
    std::string description;
    nlohmann::json arguments;
    std::shared_ptr<LoadedPlugin> plugin;
};

struct ResourceRoute {
    std::string name;
    std::string description;
    std::string uri;
    std::string mime_type;
    std::shared_ptr<LoadedPlugin> plugin;
};

class PluginRegistry final {
public:
    static std::shared_ptr<const PluginRegistry> Build(
        std::vector<std::shared_ptr<LoadedPlugin>> plugins,
        std::uint64_t generation,
        std::string* error = nullptr);

    std::uint64_t Generation() const { return generation_; }
    const std::vector<ToolRoute>& Tools() const { return tools_; }
    const std::vector<PromptRoute>& Prompts() const { return prompts_; }
    const std::vector<ResourceRoute>& Resources() const { return resources_; }

    const ToolRoute* FindTool(const std::string& name) const;
    const PromptRoute* FindPrompt(const std::string& name) const;
    const ResourceRoute* FindResource(const std::string& uri) const;
    const std::string& ToolsCapability() const { return tools_capability_; }
    const std::string& PromptsCapability() const { return prompts_capability_; }
    const std::string& ResourcesCapability() const { return resources_capability_; }

private:
    explicit PluginRegistry(std::uint64_t generation)
        : generation_(generation) {}

    std::uint64_t generation_;
    std::vector<ToolRoute> tools_;
    std::vector<PromptRoute> prompts_;
    std::vector<ResourceRoute> resources_;
    std::unordered_map<std::string, std::size_t> tools_by_name_;
    std::unordered_map<std::string, std::size_t> prompts_by_name_;
    std::unordered_map<std::string, std::size_t> resources_by_uri_;
    std::string tools_capability_;
    std::string prompts_capability_;
    std::string resources_capability_;
};

class PluginRegistryStore final {
public:
    explicit PluginRegistryStore(std::shared_ptr<const PluginRegistry> initial);

    std::shared_ptr<const PluginRegistry> Snapshot() const;
    void Publish(std::shared_ptr<const PluginRegistry> registry);

private:
    std::shared_ptr<const PluginRegistry> current_;
};

} // namespace vx::mcp

#endif // MCP_SERVER_PLUGIN_REGISTRY_H
