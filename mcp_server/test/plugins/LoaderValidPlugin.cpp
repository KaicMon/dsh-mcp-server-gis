#include "PluginAPI.h"
#include "LoaderTestSupport.h"

#include <cstring>

namespace {

PluginTool tool{
    "loader_test_tool",
    "Tool used by PluginsLoader lifecycle tests",
    R"({"type":"object","properties":{}})"
};

const char* GetName() { return "loader-valid-plugin"; }
const char* GetVersion() { return "1.0.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }

int Initialize() {
    AppendLifecycleEvent("valid:initialize");
    return 1;
}

char* HandleRequest(const char*) {
    constexpr char result[] = R"({"content":[],"isError":false})";
    auto* buffer = new char[sizeof(result)];
    std::memcpy(buffer, result, sizeof(result));
    return buffer;
}

void Shutdown() { AppendLifecycleEvent("valid:shutdown"); }
void FreeResult(char* result) { delete[] result; }
int GetToolCount() { return 1; }
const PluginTool* GetTool(int index) { return index == 0 ? &tool : nullptr; }

PluginAPI api{
    MCP_PLUGIN_ABI_VERSION,
    sizeof(PluginAPI),
    GetName,
    GetVersion,
    GetType,
    Initialize,
    HandleRequest,
    Shutdown,
    GetToolCount,
    GetTool,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    FreeResult,
    nullptr
};

} // namespace

extern "C" PLUGIN_API PluginAPI* CreatePlugin() {
    AppendLifecycleEvent("valid:create");
    return &api;
}

extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {
    AppendLifecycleEvent("valid:destroy");
}
