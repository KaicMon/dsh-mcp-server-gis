#include "tools/ToolProfile.h"

#include <iostream>

int main() {
    const auto all = vx::mcp::ToolProfile::AllowAll();
    if (!all.Allows("anything")) return 1;
    const auto gis = vx::mcp::ToolProfile::Load(TOOL_PROFILE_FILE, "gis-agent");
    if (!gis.Allows("geometry_buffer") || !gis.Allows("provider_route") ||
        gis.Allows("map_provider_status") || gis.Allows("calculator")) {
        std::cerr << "Tool profile filtering failed\n";
        return 1;
    }
    return 0;
}
