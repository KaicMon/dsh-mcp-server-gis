cmake_minimum_required(VERSION 3.20)
project(MCP_Standalone VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
option(MCP_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MCP_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(MCP_ENABLE_ASAN AND MCP_ENABLE_TSAN)
    message(FATAL_ERROR "MCP_ENABLE_ASAN and MCP_ENABLE_TSAN cannot be enabled together")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(MCP_ENABLE_ASAN)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    elseif(MCP_ENABLE_TSAN)
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    endif()
endif()

# 查找依赖
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# 公共组件
add_subdirectory(common)

# MCP 客户端库（含 RAG-MCP）
add_subdirectory(mcp_client)

# MCP 服务器
add_subdirectory(mcp_server)

# 示例程序
option(BUILD_MCP_EXAMPLES "Build MCP examples" ON)
if(BUILD_MCP_EXAMPLES)
    add_subdirectory(examples)
endif()

option(BUILD_ROUTING "Build the OSM routing engine" ON)
option(BUILD_GIS "Build the GDAL-backed GIS core" ON)
if(BUILD_ROUTING)
    add_subdirectory(routing)
endif()

# GIS support is optional because the MCP server and routing engine can still
# be built on systems that do not provide the GDAL development package.
if(BUILD_GIS)
    add_subdirectory(gis)
    add_subdirectory(mcp_server/plugins/gis-analysis)
    add_subdirectory(mcp_server/plugins/map-services)
endif()

if(BUILD_ROUTING)
    add_subdirectory(mcp_server/plugins/osm-routing)
endif()
