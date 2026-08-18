#include "PluginAPI.h"

namespace {

PluginTool tool{
    "invalid_schema_tool",
    "Tool with intentionally malformed JSON Schema",
    "{not-json"
};

const char* GetName() { return "loader-invalid-schema-plugin"; }
const char* GetVersion() { return "1.0.0"; }
PluginType GetType() { return PLUGIN_TYPE_TOOLS; }
int Initialize() { return 1; }
char* HandleRequest(const char*) { return nullptr; }
void Shutdown() {}
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

extern "C" PLUGIN_API PluginAPI* CreatePlugin() { return &api; }
extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {}
