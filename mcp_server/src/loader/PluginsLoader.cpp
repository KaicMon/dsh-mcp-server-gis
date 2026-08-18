#include "PluginsLoader.h"

#include <filesystem>
#include <utility>

namespace vx::mcp {

namespace {

void CloseLibrary(LibraryHandle handle) noexcept {
    if (!handle) {
        return;
    }
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

} // namespace

LoadedPlugin::LoadedPlugin(std::string path, PluginHostAPI host_api)
    : path_(std::move(path)), host_api_(host_api) {
    notification_context_.upstream_context = host_api.hostContext;
    notification_context_.upstream_callback = host_api.SendToClient;
    host_api_.hostContext = &notification_context_;
    host_api_.SendToClient = ForwardNotification;
}

LoadedPlugin::~LoadedPlugin() {
    Unload();
}

std::shared_ptr<LoadedPlugin> LoadedPlugin::Load(
    const std::string& path,
    const PluginHostAPI& host_api) {
    auto plugin = std::shared_ptr<LoadedPlugin>(
        new LoadedPlugin(path, host_api));

    if (!plugin->Open() ||
        !plugin->ResolveSymbols() ||
        !plugin->CreateAndInitialize()) {
        return nullptr;
    }

    LOG(INFO) << "Loaded plugin: " << plugin->Api().GetName()
              << " v" << plugin->Api().GetVersion() << std::endl;
    return plugin;
}

bool LoadedPlugin::Open() {
#ifdef _WIN32
    handle_ = LoadLibraryA(path_.c_str());
    if (!handle_) {
        LOG(ERROR) << "Failed to load plugin: " << path_
                   << " - Error " << GetLastError() << std::endl;
        return false;
    }
#else
    handle_ = dlopen(path_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        LOG(ERROR) << "Failed to load plugin: " << path_
                   << " - " << dlerror() << std::endl;
        return false;
    }
#endif
    return true;
}

bool LoadedPlugin::ResolveSymbols() {
#ifdef _WIN32
    create_func_ = reinterpret_cast<CreatePluginFunc>(
        GetProcAddress(handle_, "CreatePlugin"));
    destroy_func_ = reinterpret_cast<DestroyPluginFunc>(
        GetProcAddress(handle_, "DestroyPlugin"));
#else
    create_func_ = reinterpret_cast<CreatePluginFunc>(
        dlsym(handle_, "CreatePlugin"));
    destroy_func_ = reinterpret_cast<DestroyPluginFunc>(
        dlsym(handle_, "DestroyPlugin"));
#endif

    if (!create_func_ || !destroy_func_) {
        LOG(ERROR) << "Plugin does not export required functions: "
                   << path_ << std::endl;
        return false;
    }
    return true;
}

bool LoadedPlugin::CreateAndInitialize() {
    instance_ = create_func_();
    if (!instance_) {
        LOG(ERROR) << "CreatePlugin returned nullptr: " << path_ << std::endl;
        return false;
    }

    if (!ValidateAPI()) {
        return false;
    }

    instance_->host = &host_api_;
    if (!instance_->Initialize()) {
        LOG(ERROR) << "Plugin initialization failed: " << path_ << std::endl;
        return false;
    }

    initialized_ = true;
    return true;
}

bool LoadedPlugin::ValidateAPI() const {
    if (instance_->abiVersion != MCP_PLUGIN_ABI_VERSION) {
        LOG(ERROR) << "Unsupported plugin ABI version "
                   << instance_->abiVersion << " in " << path_ << std::endl;
        return false;
    }
    if (instance_->structSize < sizeof(PluginAPI)) {
        LOG(ERROR) << "Plugin API structure is too small: " << path_ << std::endl;
        return false;
    }
    if (!instance_->GetName || !instance_->GetVersion ||
        !instance_->GetType || !instance_->Initialize ||
        !instance_->HandleRequest || !instance_->Shutdown ||
        !instance_->FreeResult) {
        LOG(ERROR) << "Plugin API is missing required functions: "
                   << path_ << std::endl;
        return false;
    }

    const char* name = instance_->GetName();
    const char* version = instance_->GetVersion();
    if (!name || name[0] == '\0' || !version || version[0] == '\0') {
        LOG(ERROR) << "Plugin name or version is empty: " << path_ << std::endl;
        return false;
    }

    switch (instance_->GetType()) {
        case PLUGIN_TYPE_TOOLS:
            return instance_->GetToolCount && instance_->GetTool;
        case PLUGIN_TYPE_PROMPTS:
            return instance_->GetPromptCount && instance_->GetPrompt;
        case PLUGIN_TYPE_RESOURCES:
            return instance_->GetResourceCount && instance_->GetResource;
        default:
            LOG(ERROR) << "Plugin has invalid type: " << path_ << std::endl;
            return false;
    }
}

void LoadedPlugin::FreeResult(char* result) const {
    if (result) {
        instance_->FreeResult(result);
    }
}

void LoadedPlugin::DeactivateNotifications() noexcept {
    notification_context_.active.store(false, std::memory_order_release);
}

void LoadedPlugin::ForwardNotification(void* context,
                                       const char* plugin_name,
                                       const char* notification) {
    auto* state = static_cast<NotificationContext*>(context);
    if (state && state->active.load(std::memory_order_acquire) &&
        state->upstream_callback) {
        state->upstream_callback(state->upstream_context,
                                 plugin_name, notification);
    }
}

void LoadedPlugin::Unload() noexcept {
    DeactivateNotifications();
    if (instance_) {
        if (initialized_ && instance_->Shutdown) {
            instance_->Shutdown();
            initialized_ = false;
        }
        if (destroy_func_) {
            destroy_func_(instance_);
        }
        instance_ = nullptr;
    }

    if (handle_) {
        CloseLibrary(handle_);
        handle_ = nullptr;
    }
}

PluginsLoader::PluginsLoader() {
    host_api_.abiVersion = MCP_PLUGIN_ABI_VERSION;
    host_api_.structSize = sizeof(PluginHostAPI);
}

PluginsLoader::~PluginsLoader() {
    UnloadPlugins();
}

void PluginsLoader::SetHostAPI(const PluginHostAPI& host_api) {
    host_api_ = host_api;
}

bool PluginsLoader::LoadPlugins(const std::string& directory) {
    try {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string extension = entry.path().extension().string();
#ifdef _WIN32
            const bool is_library = extension == ".dll";
#elif defined(__APPLE__)
            const bool is_library = extension == ".dylib" || extension == ".so";
#else
            const bool is_library = extension == ".so";
#endif
            if (is_library) {
                LoadPlugin(entry.path().string());
            }
        }
        return true;
    } catch (const std::exception& exception) {
        LOG(ERROR) << "Error loading plugins: " << exception.what() << std::endl;
        return false;
    }
}

bool PluginsLoader::LoadPlugin(const std::string& path) {
    auto plugin = LoadedPlugin::Load(path, host_api_);
    if (!plugin) {
        return false;
    }
    plugins_.push_back(std::move(plugin));
    return true;
}

void PluginsLoader::UnloadPlugins() {
    plugins_.clear();
}

std::vector<std::shared_ptr<LoadedPlugin>>
PluginsLoader::SnapshotPlugins() const {
    return plugins_;
}

} // namespace vx::mcp
