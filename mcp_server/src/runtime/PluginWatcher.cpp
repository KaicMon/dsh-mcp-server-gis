#include "PluginWatcher.h"

#include <atomic>
#include <chrono>
#include <map>
#include <stop_token>
#include <thread>

#include "aixlog.hpp"

#ifdef __linux__
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace vx::mcp {

#ifdef __linux__
namespace {

class InotifyPluginWatcher final : public IPluginWatcher {
public:
    explicit InotifyPluginWatcher(std::filesystem::path directory)
        : directory_(std::move(directory)) {}
    ~InotifyPluginWatcher() override { Stop(); }

    bool Start(Callback callback) override {
        if (running_.exchange(true)) return false;
        callback_ = std::move(callback);
        inotify_fd_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (inotify_fd_ < 0 || pipe(wake_pipe_) != 0) {
            running_ = false;
            CloseDescriptors();
            return false;
        }
        if (!AddTree(directory_)) {
            running_ = false;
            CloseDescriptors();
            return false;
        }
        thread_ = std::jthread([this](std::stop_token stop_token) {
            Run(stop_token);
        });
        return true;
    }

    void Stop() override {
        if (!running_.exchange(false)) return;
        if (wake_pipe_[1] >= 0) {
            const char byte = 1;
            (void)write(wake_pipe_[1], &byte, 1);
        }
        thread_.request_stop();
        if (thread_.joinable()) thread_.join();
        CloseDescriptors();
    }

private:
    static constexpr std::uint32_t WatchMask =
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE |
        IN_CREATE | IN_DELETE_SELF | IN_MOVE_SELF;

    bool AddTree(const std::filesystem::path& root) {
        if (!AddDirectory(root)) return false;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_directory()) AddDirectory(entry.path());
        }
        return true;
    }

    bool AddDirectory(const std::filesystem::path& directory) {
        const int wd = inotify_add_watch(inotify_fd_, directory.c_str(), WatchMask);
        if (wd < 0) return false;
        watches_[wd] = directory;
        return true;
    }

    void Run(std::stop_token stop_token) {
        alignas(inotify_event) char buffer[64 * 1024];
        while (running_ && !stop_token.stop_requested()) {
            pollfd descriptors[2]{{inotify_fd_, POLLIN, 0},
                                  {wake_pipe_[0], POLLIN, 0}};
            const int ready = poll(descriptors, 2, -1);
            if (ready <= 0 || descriptors[1].revents & POLLIN) break;
            if (!(descriptors[0].revents & POLLIN)) continue;
            const ssize_t bytes = read(inotify_fd_, buffer, sizeof(buffer));
            if (bytes <= 0) continue;
            std::size_t offset = 0;
            while (offset < static_cast<std::size_t>(bytes)) {
                const auto* event = reinterpret_cast<const inotify_event*>(
                    buffer + offset);
                Handle(*event);
                offset += sizeof(inotify_event) + event->len;
            }
        }
    }

    void Handle(const inotify_event& event) {
        if (event.mask & IN_Q_OVERFLOW) {
            callback_({PluginFileEvent::Kind::RescanRequired, directory_});
            return;
        }
        const auto found = watches_.find(event.wd);
        if (found == watches_.end()) return;
        const auto path = event.len ? found->second / event.name : found->second;
        if (event.mask & IN_ISDIR) {
            if (event.mask & (IN_CREATE | IN_MOVED_TO)) {
                std::error_code ignored;
                if (std::filesystem::is_directory(path, ignored)) AddTree(path);
            }
            return;
        }
        if (event.mask & (IN_DELETE | IN_MOVED_FROM)) {
            callback_({PluginFileEvent::Kind::Removed, path});
        } else if (event.mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
            callback_({PluginFileEvent::Kind::CreatedOrModified, path});
        }
    }

    void CloseDescriptors() {
        if (inotify_fd_ >= 0) close(inotify_fd_);
        if (wake_pipe_[0] >= 0) close(wake_pipe_[0]);
        if (wake_pipe_[1] >= 0) close(wake_pipe_[1]);
        inotify_fd_ = wake_pipe_[0] = wake_pipe_[1] = -1;
        watches_.clear();
    }

    std::filesystem::path directory_;
    Callback callback_;
    std::atomic<bool> running_{false};
    int inotify_fd_ = -1;
    int wake_pipe_[2]{-1, -1};
    std::map<int, std::filesystem::path> watches_;
    std::jthread thread_;
};

} // namespace
#endif

std::unique_ptr<IPluginWatcher> CreatePluginWatcher(
    const std::filesystem::path& directory) {
#ifdef __linux__
    return std::make_unique<InotifyPluginWatcher>(directory);
#else
    (void)directory;
    return nullptr;
#endif
}

} // namespace vx::mcp
