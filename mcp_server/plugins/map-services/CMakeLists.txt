add_library(map-services SHARED MapServicesPlugin.cpp)

target_link_libraries(map-services PRIVATE gis_providers gis_results gis_cache)
target_include_directories(map-services PRIVATE
    ${CMAKE_SOURCE_DIR}/mcp_server/include
    ${CMAKE_SOURCE_DIR}/mcp_server/src/interface
)
target_compile_features(map-services PRIVATE cxx_std_20)
target_compile_options(map-services PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(map-services PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mcp_server/plugins/map-services"
)

if(BUILD_TESTING)
    add_executable(map_services_plugin_test test/MapServicesPluginTest.cpp)
    target_include_directories(map_services_plugin_test PRIVATE
        ${CMAKE_SOURCE_DIR}/mcp_server/include
        ${CMAKE_SOURCE_DIR}/mcp_server/src/interface
    )
    target_link_libraries(map_services_plugin_test PRIVATE
        ${CMAKE_DL_LIBS}
        routing_osm
    )
    target_compile_features(map_services_plugin_test PRIVATE cxx_std_20)
    target_compile_definitions(map_services_plugin_test PRIVATE
        MAP_SERVICES_PLUGIN_PATH="$<TARGET_FILE:map-services>"
        ROUTING_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/routing/data/test"
    )
    add_dependencies(map_services_plugin_test map-services)
    add_test(NAME gis_map_services_plugin COMMAND map_services_plugin_test)
endif()
