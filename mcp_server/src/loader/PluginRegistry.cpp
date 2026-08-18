#include "PluginRegistry.h"

#include <atomic>
#include <exception>
#include <utility>

namespace vx::mcp {
namespace {

bool RequireString(const char* value,
                   const std::string& field,
                   const std::string& plugin_name,
                   std::string* error) {
    if (value && value[0] != '\0') {
        return true;
    }
    if (error) {
        *error = "Plugin '" + plugin_name + "' has an empty " + field;
    }
    return false;
}

bool RequireCount(int count,
                  const std::string& plugin_name,
                  std::string* error) {
    if (count >= 0) {
        return true;
    }
    if (error) {
        *error = "Plugin '" + plugin_name + "' returned a negative item count";
    }
    return false;
}

template<typename Map>
bool InsertUnique(Map& index,
                  const std::string& key,
                  std::size_t position,
                  const std::string& kind,
                  std::string* error) {
    if (index.emplace(key, position).second) {
        return true;
    }
    if (error) {
        *error = "Duplicate " + kind + ": " + key;
    }
    return false;
}

} // namespace

std::shared_ptr<const PluginRegistry> PluginRegistry::Build(
    std::vector<std::shared_ptr<LoadedPlugin>> plugins,
    std::uint64_t generation,
    std::string* error) {
    auto registry = std::shared_ptr<PluginRegistry>(new PluginRegistry(generation));

    try {
        for (const auto& plugin : plugins) {
            if (!plugin) {
                if (error) {
                    *error = "Plugin owner is null";
                }
                return nullptr;
            }

            PluginAPI& api = plugin->Api();
            const std::string plugin_name = api.GetName();
            switch (api.GetType()) {
                case PLUGIN_TYPE_TOOLS: {
                    const int count = api.GetToolCount();
                    if (!RequireCount(count, plugin_name, error)) return nullptr;
                    for (int i = 0; i < count; ++i) {
                        const PluginTool* item = api.GetTool(i);
                        if (!item ||
                            !RequireString(item->name, "tool name", plugin_name, error) ||
                            !RequireString(item->description, "tool description", plugin_name, error) ||
                            !RequireString(item->inputSchema, "tool inputSchema", plugin_name, error)) {
                            return nullptr;
                        }
                        ToolRoute route{
                            item->name,
                            item->description,
                            nlohmann::json::parse(item->inputSchema),
                            plugin
                        };
                        if (!InsertUnique(registry->tools_by_name_, route.name,
                                          registry->tools_.size(), "tool name", error)) {
                            return nullptr;
                        }
                        registry->tools_.push_back(std::move(route));
                    }
                    break;
                }
                case PLUGIN_TYPE_PROMPTS: {
                    const int count = api.GetPromptCount();
                    if (!RequireCount(count, plugin_name, error)) return nullptr;
                    for (int i = 0; i < count; ++i) {
                        const PluginPrompt* item = api.GetPrompt(i);
                        if (!item ||
                            !RequireString(item->name, "prompt name", plugin_name, error) ||
                            !RequireString(item->description, "prompt description", plugin_name, error) ||
                            !RequireString(item->arguments, "prompt arguments", plugin_name, error)) {
                            return nullptr;
                        }
                        PromptRoute route{
                            item->name,
                            item->description,
                            nlohmann::json::parse(item->arguments),
                            plugin
                        };
                        if (!InsertUnique(registry->prompts_by_name_, route.name,
                                          registry->prompts_.size(), "prompt name", error)) {
                            return nullptr;
                        }
                        registry->prompts_.push_back(std::move(route));
                    }
                    break;
                }
                case PLUGIN_TYPE_RESOURCES: {
                    const int count = api.GetResourceCount();
                    if (!RequireCount(count, plugin_name, error)) return nullptr;
                    for (int i = 0; i < count; ++i) {
                        const PluginResource* item = api.GetResource(i);
                        if (!item ||
                            !RequireString(item->name, "resource name", plugin_name, error) ||
                            !RequireString(item->description, "resource description", plugin_name, error) ||
                            !RequireString(item->uri, "resource URI", plugin_name, error) ||
                            !RequireString(item->mime, "resource MIME type", plugin_name, error)) {
                            return nullptr;
                        }
                        ResourceRoute route{
                            item->name, item->description, item->uri, item->mime, plugin
                        };
                        if (!InsertUnique(registry->resources_by_uri_, route.uri,
                                          registry->resources_.size(), "resource URI", error)) {
                            return nullptr;
                        }
                        registry->resources_.push_back(std::move(route));
                    }
                    break;
                }
                default:
                    if (error) *error = "Plugin '" + plugin_name + "' has an invalid type";
                    return nullptr;
            }
        }
    } catch (const nlohmann::json::parse_error& exception) {
        if (error) {
            *error = std::string("Plugin metadata contains invalid JSON: ") + exception.what();
        }
        return nullptr;
    } catch (const std::exception& exception) {
        if (error) {
            *error = std::string("Failed to build plugin registry: ") + exception.what();
        }
        return nullptr;
    }

    nlohmann::ordered_json tools = nlohmann::ordered_json::array();
    for (const auto& route : registry->tools_) {
        tools.push_back({route.name, route.description, route.input_schema});
    }
    nlohmann::ordered_json prompts = nlohmann::ordered_json::array();
    for (const auto& route : registry->prompts_) {
        prompts.push_back({route.name, route.description, route.arguments});
    }
    nlohmann::ordered_json resources = nlohmann::ordered_json::array();
    for (const auto& route : registry->resources_) {
        resources.push_back({route.name, route.description,
                             route.uri, route.mime_type});
    }
    registry->tools_capability_ = tools.dump();
    registry->prompts_capability_ = prompts.dump();
    registry->resources_capability_ = resources.dump();
    return registry;
}

const ToolRoute* PluginRegistry::FindTool(const std::string& name) const {
    const auto found = tools_by_name_.find(name);
    return found == tools_by_name_.end() ? nullptr : &tools_[found->second];
}

const PromptRoute* PluginRegistry::FindPrompt(const std::string& name) const {
    const auto found = prompts_by_name_.find(name);
    return found == prompts_by_name_.end() ? nullptr : &prompts_[found->second];
}

const ResourceRoute* PluginRegistry::FindResource(const std::string& uri) const {
    const auto found = resources_by_uri_.find(uri);
    return found == resources_by_uri_.end() ? nullptr : &resources_[found->second];
}

PluginRegistryStore::PluginRegistryStore(
    std::shared_ptr<const PluginRegistry> initial)
    : current_(std::move(initial)) {}

std::shared_ptr<const PluginRegistry> PluginRegistryStore::Snapshot() const {
    return std::atomic_load_explicit(&current_, std::memory_order_acquire);
}

void PluginRegistryStore::Publish(
    std::shared_ptr<const PluginRegistry> registry) {
    std::atomic_store_explicit(
        &current_, std::move(registry), std::memory_order_release);
}

} // namespace vx::mcp
