#include "PluginAPI.h"

extern "C" PLUGIN_API PluginAPI* CreatePlugin() {
    return nullptr;
}

extern "C" PLUGIN_API void DestroyPlugin(PluginAPI*) {
}
