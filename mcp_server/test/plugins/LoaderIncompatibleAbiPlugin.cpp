#include "PluginAPI.h"

namespace {

const char* GetName() { return "loader-incompatible-abi"; }
const char* GetVersion() { return "1.0.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }
int Initialize() { return 1; }
char* HandleRequest(const char*) { return nullptr; }
void Shutdown() {}
int GetToolCount() { return 0; }
const PluginTool* GetTool(int) { return nullptr; }
void FreeResult(char* result) { delete[] result; }

PluginAPI api{
    MCP_PLUGIN_ABI_VERSION + 1,
    sizeof(PluginAPI),
    GetName, GetVersion, GetType, Initialize, HandleRequest, Shutdown,
    GetToolCount, GetTool,
    nullptr, nullptr, nullptr, nullptr,
    FreeResult, nullptr
};

} // namespace

extern "C" PLUGIN_API PluginAPI* CreatePlugin() { return &api; }
extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {}
