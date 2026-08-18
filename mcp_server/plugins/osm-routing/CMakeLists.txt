add_library(osm-routing SHARED OsmRoutingPlugin.cpp)
target_link_libraries(osm-routing PRIVATE routing_osm Threads::Threads)
if(TARGET gis_results)
    target_link_libraries(osm-routing PRIVATE gis_results)
    target_compile_definitions(osm-routing PRIVATE GIS_HAS_RESULT_POLICY=1)
endif()
target_include_directories(osm-routing PRIVATE
    ${CMAKE_SOURCE_DIR}/mcp_server/include
    ${CMAKE_SOURCE_DIR}/mcp_server/src/interface
)
target_compile_features(osm-routing PRIVATE cxx_std_20)
set_target_properties(osm-routing PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mcp_server/plugins/osm-routing"
)

if(BUILD_TESTING)
    add_executable(osm_routing_plugin_test OsmRoutingPluginTest.cpp)
    target_link_libraries(osm_routing_plugin_test PRIVATE
        routing_osm ${CMAKE_DL_LIBS})
    target_include_directories(osm_routing_plugin_test PRIVATE
        ${CMAKE_SOURCE_DIR}/mcp_server/include
        ${CMAKE_SOURCE_DIR}/mcp_server/src/interface)
    target_compile_features(osm_routing_plugin_test PRIVATE cxx_std_20)
    target_compile_definitions(osm_routing_plugin_test PRIVATE
        OSM_ROUTING_PLUGIN_PATH="$<TARGET_FILE:osm-routing>"
        ROUTING_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/routing/data/test")
    add_dependencies(osm_routing_plugin_test osm-routing)
    add_test(NAME gis_osm_routing_plugin COMMAND osm_routing_plugin_test)
endif()
