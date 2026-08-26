#include "PluginRuntime.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>
#include <system_error>
#include <utility>

#include "aixlog.hpp"
#include "json.hpp"

namespace vx::mcp {
namespace {

bool IsLibrary(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.extension() == ".dll";
#elif defined(__APPLE__)
    return path.extension() == ".dylib" || path.extension() == ".so";
#else
    return path.extension() == ".so";
#endif
}

std::string NormalizeExistingPath(const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(path).string();
}

std::string PathId(const std::string& source_key) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16)
           << std::hash<std::string>{}(source_key);
    return output.str();
}

std::string Sha256File(const std::filesystem::path& path, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) *error = "Cannot open plugin source: " + path.string();
        return {};
    }

    EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
    if (!raw_context) {
        if (error) *error = "Cannot allocate SHA-256 context";
        return {};
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>
        context(raw_context, EVP_MD_CTX_free);
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        if (error) *error = "Cannot initialize SHA-256";
        return {};
    }

    char buffer[64 * 1024];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        if (count > 0 &&
            EVP_DigestUpdate(context.get(), buffer,
                             static_cast<std::size_t>(count)) != 1) {
            if (error) *error = "Cannot update SHA-256";
            return {};
        }
    }
    if (!input.eof()) {
        if (error) *error = "Cannot read plugin source: " + path.string();
        return {};
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest, &length) != 1) {
        if (error) *error = "Cannot finalize SHA-256";
        return {};
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < length; ++i) {
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

} // namespace

PluginRuntime::~PluginRuntime() {
    Shutdown();
}

bool PluginRuntime::Initialize(const PluginRuntimeConfig& config,
                               std::string* error) {
    std::lock_guard<std::mutex> lock(update_mutex_);
    if (initialized_) {
        if (error) *error = "PluginRuntime is already initialized";
        return false;
    }
    config_ = config;
    try {
        staging_sequence_ = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        std::filesystem::create_directories(config_.staging_directory);
        auto empty = PluginRegistry::Build({}, 0, error);
        if (!empty) return false;
        registry_store_.Publish(std::move(empty));
        initialized_ = true;

        if (!std::filesystem::exists(config_.source_directory)) {
            if (error) {
                *error = "Plugin source directory does not exist: " +
                         config_.source_directory.string();
            }
            initialized_ = false;
            return false;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 config_.source_directory)) {
            if (entry.is_regular_file() && IsLibrary(entry.path())) {
                ReloadResult result = ReloadLocked(entry.path());
                if (!result.success) {
                    LOG(ERROR) << "Initial plugin load failed: "
                               << result.message << std::endl;
                }
            }
        }
        return true;
    } catch (const std::exception& exception) {
        initialized_ = false;
        if (error) *error = exception.what();
        return false;
    }
}

void PluginRuntime::Shutdown() {
    StopWatching();
    std::lock_guard<std::mutex> lock(update_mutex_);
    if (!initialized_) return;
    std::string ignored;
    auto empty = PluginRegistry::Build({}, generation_ + 1, &ignored);
    registry_store_.Publish(std::move(empty));
    for (auto& [_, active] : active_plugins_) {
        active.plugin->DeactivateNotifications();
    }
    active_plugins_.clear();
    ++generation_;
    initialized_ = false;
}

bool PluginRuntime::StartWatching(std::string* error) {
    {
        std::lock_guard<std::mutex> lock(update_mutex_);
        if (!initialized_) {
            if (error) *error = "PluginRuntime is not initialized";
            return false;
        }
    }
    std::unique_lock<std::mutex> event_lock(event_mutex_);
    if (event_running_) return true;
    watcher_ = CreatePluginWatcher(config_.source_directory);
    if (!watcher_) {
        if (error) *error = "No plugin watcher is available on this platform";
        return false;
    }
    event_running_ = true;
    event_thread_ = std::jthread([this](std::stop_token stop_token) {
        EventLoop(stop_token);
    });
    if (!watcher_->Start([this](const PluginFileEvent& event) {
            QueueFileEvent(event);
        })) {
        event_running_ = false;
        event_lock.unlock();
        event_cv_.notify_all();
        event_thread_.request_stop();
        event_thread_.join();
        event_lock.lock();
        watcher_.reset();
        if (error) *error = "Failed to start plugin watcher";
        return false;
    }
    return true;
}

void PluginRuntime::StopWatching() {
    std::unique_ptr<IPluginWatcher> watcher;
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        if (!event_running_ && !watcher_) return;
        event_running_ = false;
        pending_events_.clear();
        watcher = std::move(watcher_);
    }
    if (watcher) watcher->Stop();
    event_thread_.request_stop();
    event_cv_.notify_all();
    if (event_thread_.joinable()) event_thread_.join();
}

void PluginRuntime::QueueFileEvent(const PluginFileEvent& event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (!event_running_) return;
    const std::string key = event.kind == PluginFileEvent::Kind::RescanRequired
        ? std::string("<rescan>")
        : std::filesystem::absolute(event.path).lexically_normal().string();
    const auto delay = event.kind == PluginFileEvent::Kind::Removed
        ? config_.delete_grace : config_.debounce;
    pending_events_[key] = {event.kind, std::chrono::steady_clock::now() + delay};
    event_cv_.notify_one();
}

void PluginRuntime::EventLoop(std::stop_token stop_token) {
    std::unique_lock<std::mutex> lock(event_mutex_);
    while (event_running_ && !stop_token.stop_requested()) {
        if (pending_events_.empty()) {
            event_cv_.wait(lock, [this] {
                return !event_running_ || !pending_events_.empty();
            });
            continue;
        }
        auto next = pending_events_.begin();
        for (auto it = pending_events_.begin(); it != pending_events_.end(); ++it) {
            if (it->second.ready_at < next->second.ready_at) next = it;
        }
        if (event_cv_.wait_until(lock, next->second.ready_at) !=
            std::cv_status::timeout) {
            continue;
        }
        const std::string path = next->first;
        const auto kind = next->second.kind;
        pending_events_.erase(next);
        lock.unlock();
        if (kind == PluginFileEvent::Kind::RescanRequired) {
            Reconcile();
        } else if (kind == PluginFileEvent::Kind::Removed) {
            std::error_code ignored;
            if (!std::filesystem::exists(path, ignored)) Remove(path);
            else Reload(path);
        } else {
            Reload(path);
        }
        lock.lock();
    }
}

void PluginRuntime::Reconcile() {
    std::map<std::string, std::filesystem::path> disk_plugins;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 config_.source_directory)) {
            if (entry.is_regular_file() && IsLibrary(entry.path())) {
                disk_plugins[NormalizeExistingPath(entry.path())] = entry.path();
            }
        }
    } catch (const std::exception& exception) {
        LOG(ERROR) << "Plugin rescan failed: " << exception.what() << std::endl;
        return;
    }
    std::vector<std::string> active_paths;
    {
        std::lock_guard<std::mutex> lock(update_mutex_);
        for (const auto& [path, _] : active_plugins_) active_paths.push_back(path);
    }
    for (const auto& [_, path] : disk_plugins) Reload(path);
    for (const auto& path : active_paths) {
        if (disk_plugins.find(path) == disk_plugins.end()) Remove(path);
    }
}

std::shared_ptr<const PluginRegistry> PluginRuntime::Snapshot() const {
    return registry_store_.Snapshot();
}

ReloadResult PluginRuntime::Reload(const std::filesystem::path& source_path) {
    std::lock_guard<std::mutex> lock(update_mutex_);
    return ReloadLocked(source_path);
}

ReloadResult PluginRuntime::ReloadLocked(
    const std::filesystem::path& source_path) {
    ReloadResult result;
    const std::uint64_t reload_id = ++reload_sequence_;
    result.generation = generation_;
    if (!initialized_) {
        result.message = "PluginRuntime is not initialized";
        return result;
    }
    if (!std::filesystem::is_regular_file(source_path) ||
        !IsLibrary(source_path)) {
        result.message = "Plugin source is not a supported library: " +
                         source_path.string();
        return result;
    }

    const std::string source_key = NormalizeExistingPath(source_path);
    LOG(INFO) << "reload_id=" << reload_id << " stage=detected source="
              << source_key << std::endl;
    std::string error;
    const std::string sha256 = Sha256File(source_path, &error);
    if (sha256.empty()) {
        result.message = error;
        return result;
    }
    const auto existing = active_plugins_.find(source_key);
    if (existing != active_plugins_.end() && existing->second.sha256 == sha256) {
        result.success = true;
        result.message = "Plugin content is unchanged";
        return result;
    }

    const std::uint64_t candidate_generation = generation_ + 1;
    const auto staged_path = Stage(source_path, source_key, sha256,
                                   ++staging_sequence_, &error);
    if (staged_path.empty()) {
        result.message = error;
        LOG(ERROR) << "reload_id=" << reload_id << " stage=staged result=failed error="
                   << result.message << std::endl;
        return result;
    }
    LOG(INFO) << "reload_id=" << reload_id << " stage=staged path="
              << staged_path.string() << std::endl;
    auto candidate = LoadedPlugin::Load(staged_path.string(), config_.host_api);
    if (!candidate) {
        std::error_code ignored;
        std::filesystem::remove_all(staged_path.parent_path(), ignored);
        result.message = "Candidate plugin failed ABI or initialization checks";
        return result;
    }

    auto reject_candidate = [&](const std::string& message) {
        candidate.reset();
        std::error_code ignored;
        std::filesystem::remove_all(staged_path.parent_path(), ignored);
        result.message = message;
        return result;
    };

    const std::string candidate_name = candidate->Api().GetName();
    if (existing != active_plugins_.end() &&
        existing->second.plugin_name != candidate_name) {
        return reject_candidate("Plugin name changed for the same source path");
    }
    for (const auto& [key, active] : active_plugins_) {
        if (key != source_key && active.plugin_name == candidate_name) {
            return reject_candidate("Duplicate plugin name: " + candidate_name);
        }
    }

    auto proposed = active_plugins_;
    proposed[source_key] = ActivePlugin{
        source_key,
        staged_path,
        sha256,
        candidate_name,
        candidate->Api().GetVersion(),
        candidate_generation,
        std::move(candidate)
    };
    auto registry = BuildRegistry(proposed, candidate_generation, &error);
    if (!registry) {
        proposed.clear();
        std::error_code ignored;
        std::filesystem::remove_all(staged_path.parent_path(), ignored);
        result.message = error;
        return result;
    }

    const auto previous_registry = registry_store_.Snapshot();
    std::shared_ptr<LoadedPlugin> retired_plugin;
    if (existing != active_plugins_.end()) retired_plugin = existing->second.plugin;
    active_plugins_.swap(proposed);
    generation_ = candidate_generation;
    registry_store_.Publish(std::move(registry));
    if (retired_plugin) retired_plugin->DeactivateNotifications();
    const auto& published = active_plugins_.at(source_key);
    staged_history_.push_back({source_key, published.staged_path.parent_path(),
                               published.plugin});
    MarkMetadataValidated(published);
    const auto current_registry = registry_store_.Snapshot();
    NotifyCapabilityChanges(previous_registry, current_registry);
    CollectGarbage();
    result.success = true;
    result.changed = true;
    result.generation = generation_;
    result.message = "Plugin Registry published";
    LOG(INFO) << "reload_id=" << reload_id << " stage=published generation="
              << generation_ << " source=" << source_key << std::endl;
    return result;
}

ReloadResult PluginRuntime::Remove(const std::filesystem::path& source_path) {
    std::lock_guard<std::mutex> lock(update_mutex_);
    ReloadResult result;
    result.generation = generation_;
    if (!initialized_) {
        result.message = "PluginRuntime is not initialized";
        return result;
    }
    const std::string source_key = NormalizeExistingPath(source_path);
    if (active_plugins_.find(source_key) == active_plugins_.end()) {
        result.success = true;
        result.message = "Plugin is not active";
        return result;
    }

    const auto previous_registry = registry_store_.Snapshot();
    auto retired_plugin = active_plugins_.at(source_key).plugin;
    auto proposed = active_plugins_;
    proposed.erase(source_key);
    std::string error;
    const std::uint64_t candidate_generation = generation_ + 1;
    auto registry = BuildRegistry(proposed, candidate_generation, &error);
    if (!registry) {
        result.message = error;
        return result;
    }
    active_plugins_.swap(proposed);
    generation_ = candidate_generation;
    registry_store_.Publish(std::move(registry));
    retired_plugin->DeactivateNotifications();
    NotifyCapabilityChanges(previous_registry, registry_store_.Snapshot());
    CollectGarbage();
    result.success = true;
    result.changed = true;
    result.generation = generation_;
    result.message = "Plugin removed from Registry";
    return result;
}

PluginRuntimeStatus PluginRuntime::Status() const {
    PluginRuntimeStatus status;
    {
        std::lock_guard<std::mutex> lock(update_mutex_);
        status.generation = generation_;
        status.active_plugins = active_plugins_.size();
    }
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        status.watching = event_running_;
    }
    return status;
}

void PluginRuntime::NotifyCapabilityChanges(
    const std::shared_ptr<const PluginRegistry>& previous,
    const std::shared_ptr<const PluginRegistry>& current) const {
    if (!config_.capability_notification || !previous || !current) return;
    if (previous->ToolsCapability() != current->ToolsCapability()) {
        config_.capability_notification("notifications/tools/list_changed");
    }
    if (previous->PromptsCapability() != current->PromptsCapability()) {
        config_.capability_notification("notifications/prompts/list_changed");
    }
    if (previous->ResourcesCapability() != current->ResourcesCapability()) {
        config_.capability_notification("notifications/resources/list_changed");
    }
}

void PluginRuntime::MarkMetadataValidated(const ActivePlugin& plugin) const {
    try {
        const auto path = plugin.staged_path.parent_path() / "metadata.json";
        nlohmann::ordered_json metadata;
        {
            std::ifstream input(path);
            input >> metadata;
        }
        metadata["pluginName"] = plugin.plugin_name;
        metadata["pluginVersion"] = plugin.plugin_version;
        metadata["publishedGeneration"] = plugin.generation;
        metadata["validated"] = true;
        std::ofstream output(path, std::ios::trunc);
        output << metadata.dump(2) << '\n';
    } catch (const std::exception& exception) {
        LOG(WARNING) << "Cannot update staging metadata: "
                     << exception.what() << std::endl;
    }
}

void PluginRuntime::CollectGarbage() {
    std::map<std::string, std::size_t> remaining_to_keep;
    for (const auto& item : staged_history_) {
        remaining_to_keep[item.source_key] = config_.retain_generations;
    }
    std::vector<bool> keep(staged_history_.size(), false);
    for (std::size_t i = staged_history_.size(); i-- > 0;) {
        auto& remaining = remaining_to_keep[staged_history_[i].source_key];
        if (remaining > 0) {
            keep[i] = true;
            --remaining;
        }
    }
    for (std::size_t i = 0; i < staged_history_.size();) {
        auto& item = staged_history_[i];
        if (!keep[i] && item.plugin.expired()) {
            std::error_code error;
            std::filesystem::remove_all(item.directory, error);
            if (error) {
                LOG(WARNING) << "Cannot remove retired staging directory "
                             << item.directory.string() << ": "
                             << error.message() << std::endl;
                ++i;
            } else {
                staged_history_.erase(staged_history_.begin() + i);
                keep.erase(keep.begin() + i);
            }
        } else {
            ++i;
        }
    }
}

std::shared_ptr<const PluginRegistry> PluginRuntime::BuildRegistry(
    const std::map<std::string, ActivePlugin>& plugins,
    std::uint64_t generation,
    std::string* error) const {
    std::vector<std::shared_ptr<LoadedPlugin>> owners;
    owners.reserve(plugins.size());
    for (const auto& [_, active] : plugins) owners.push_back(active.plugin);
    return PluginRegistry::Build(std::move(owners), generation, error);
}

std::filesystem::path PluginRuntime::Stage(
    const std::filesystem::path& source_path,
    const std::string& source_key,
    const std::string& sha256,
    std::uint64_t generation,
    std::string* error) const {
    try {
        const auto source_root = config_.staging_directory / PathId(source_key);
        std::ostringstream generation_name;
        generation_name << "generation-" << std::setfill('0') << std::setw(8)
                        << generation;
        const auto final_directory = source_root / generation_name.str();
        const auto temporary_directory =
            source_root / (generation_name.str() + ".tmp-" + sha256.substr(0, 12));
        std::filesystem::create_directories(source_root);
        std::error_code cleanup_error;
        std::filesystem::remove_all(temporary_directory, cleanup_error);
        if (std::filesystem::exists(final_directory)) {
            if (error) *error = "Staging generation already exists: " +
                                final_directory.string();
            return {};
        }
        std::filesystem::create_directories(temporary_directory);
        const auto staged_library = temporary_directory / source_path.filename();
        std::filesystem::copy_file(source_path, staged_library,
                                   std::filesystem::copy_options::none);

        std::string copied_hash_error;
        if (Sha256File(staged_library, &copied_hash_error) != sha256) {
            std::filesystem::remove_all(temporary_directory, cleanup_error);
            if (error) *error = "Staged plugin hash mismatch";
            return {};
        }
        nlohmann::ordered_json metadata{
            {"sourcePath", source_key},
            {"sha256", sha256},
            {"size", std::filesystem::file_size(source_path)},
            {"mtime", std::filesystem::last_write_time(source_path)
                          .time_since_epoch().count()},
            {"generation", generation},
            {"abiVersion", MCP_PLUGIN_ABI_VERSION},
            {"validated", false}
        };
        std::ofstream metadata_file(temporary_directory / "metadata.json");
        metadata_file << metadata.dump(2) << '\n';
        metadata_file.close();
        if (!metadata_file) {
            std::filesystem::remove_all(temporary_directory, cleanup_error);
            if (error) *error = "Cannot write staging metadata";
            return {};
        }
        std::filesystem::rename(temporary_directory, final_directory);
        return final_directory / source_path.filename();
    } catch (const std::exception& exception) {
        if (error) *error = std::string("Staging failed: ") + exception.what();
        return {};
    }
}

} // namespace vx::mcp
