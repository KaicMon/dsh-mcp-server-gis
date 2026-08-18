#include "PluginAPI.h"
#include "LoaderTestSupport.h"

namespace {

const char* GetName() { return "loader-init-failure-plugin"; }
const char* GetVersion() { return "1.0.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }

int Initialize() {
    AppendLifecycleEvent("failure:initialize");
    return 0;
}

char* HandleRequest(const char*) { return nullptr; }
void Shutdown() { AppendLifecycleEvent("failure:shutdown"); }
void FreeResult(char* result) { delete[] result; }
int GetToolCount() { return 0; }
const PluginTool* GetTool(int) { return nullptr; }

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
    AppendLifecycleEvent("failure:create");
    return &api;
}

extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {
    AppendLifecycleEvent("failure:destroy");
}
