#include "PluginAPI.h"

#include <cstdlib>
#include <cstring>

namespace {
PluginTool tool{"runtime_tool", "Runtime reload test tool", R"({"type":"object"})"};
extern PluginAPI api;
const char* GetName() { return "runtime-test-plugin"; }
const char* GetVersion() { return "1.0.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }
int Initialize() { return 1; }
char* HandleRequest(const char*) {
    if (api.host && api.host->SendToClient) {
        api.host->SendToClient(api.host->hostContext, GetName(),
            R"({"jsonrpc":"2.0","method":"notifications/message"})");
    }
    return ::strdup(R"({"content":[{"type":"text","text":"v1"}]})");
}
void Shutdown() {}
void FreeResult(char* result) { std::free(result); }
int GetToolCount() { return 1; }
const PluginTool* GetTool(int index) { return index == 0 ? &tool : nullptr; }
PluginAPI api{MCP_PLUGIN_ABI_VERSION, sizeof(PluginAPI), GetName, GetVersion,
              GetType, Initialize, HandleRequest, Shutdown, GetToolCount,
              GetTool, nullptr, nullptr, nullptr, nullptr, FreeResult, nullptr};
}
extern "C" PLUGIN_API PluginAPI* CreatePlugin() { return &api; }
extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {}
