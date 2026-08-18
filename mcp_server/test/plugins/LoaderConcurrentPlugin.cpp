#include "PluginAPI.h"
#include "LoaderTestSupport.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {

std::atomic<bool> running{false};
std::thread worker;
PluginTool tool{"concurrent_test_tool", "Controllable concurrent test tool", R"({"type":"object"})"};

const char* GetName() { return "loader-concurrent-plugin"; }
const char* GetVersion() { return "1.0.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }

int Initialize() {
    AppendLifecycleEvent("concurrent:initialize");
    running.store(true);
    worker = std::thread([] {
        AppendLifecycleEvent("concurrent:worker-start");
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        AppendLifecycleEvent("concurrent:worker-stop");
    });
    return 1;
}

char* HandleRequest(const char*) {
    AppendLifecycleEvent("concurrent:handle-start");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    AppendLifecycleEvent("concurrent:handle-end");
    constexpr char result[] = R"({"content":[],"isError":false})";
    auto* buffer = new char[sizeof(result)];
    std::memcpy(buffer, result, sizeof(result));
    return buffer;
}

void Shutdown() {
    AppendLifecycleEvent("concurrent:shutdown");
    running.store(false);
    if (worker.joinable()) {
        worker.join();
    }
}

void FreeResult(char* result) { delete[] result; }

int GetToolCount() { return 1; }
const PluginTool* GetTool(int index) { return index == 0 ? &tool : nullptr; }
PluginAPI api{
    MCP_PLUGIN_ABI_VERSION, sizeof(PluginAPI),
    GetName, GetVersion, GetType, Initialize, HandleRequest, Shutdown,
    GetToolCount, GetTool,
    nullptr, nullptr, nullptr, nullptr, FreeResult, nullptr
};

} // namespace

extern "C" PLUGIN_API PluginAPI* CreatePlugin() {
    AppendLifecycleEvent("concurrent:create");
    return &api;
}

extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {
    AppendLifecycleEvent("concurrent:destroy");
}
