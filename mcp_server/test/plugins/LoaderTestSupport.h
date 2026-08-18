#pragma once

#include <cstdlib>
#include <fstream>
#include <string_view>

inline void AppendLifecycleEvent(std::string_view event) {
    const char* path = std::getenv("MCP_TEST_LIFECYCLE_LOG");
    if (!path || path[0] == '\0') {
        return;
    }

    std::ofstream output(path, std::ios::app);
    output << event << '\n';
}
