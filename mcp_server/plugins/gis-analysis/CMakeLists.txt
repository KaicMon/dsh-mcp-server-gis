add_library(gis-analysis SHARED GisAnalysisPlugin.cpp)

target_link_libraries(gis-analysis PRIVATE gis_core gis_results)
target_include_directories(gis-analysis PRIVATE
    ${CMAKE_SOURCE_DIR}/mcp_server/include
    ${CMAKE_SOURCE_DIR}/mcp_server/src/interface
)
target_compile_features(gis-analysis PRIVATE cxx_std_20)
target_compile_options(gis-analysis PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(gis-analysis PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mcp_server/plugins/gis-analysis"
)

if(BUILD_TESTING)
    add_executable(gis_analysis_plugin_test test/GisAnalysisPluginTest.cpp)
    target_include_directories(gis_analysis_plugin_test PRIVATE
        ${CMAKE_SOURCE_DIR}/mcp_server/include
        ${CMAKE_SOURCE_DIR}/mcp_server/src/interface
    )
    target_link_libraries(gis_analysis_plugin_test PRIVATE ${CMAKE_DL_LIBS})
    target_compile_features(gis_analysis_plugin_test PRIVATE cxx_std_20)
    target_compile_definitions(gis_analysis_plugin_test PRIVATE
        GIS_ANALYSIS_PLUGIN_PATH="$<TARGET_FILE:gis-analysis>"
        GIS_ANALYSIS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/data"
    )
    add_dependencies(gis_analysis_plugin_test gis-analysis)
    add_test(NAME gis_analysis_plugin COMMAND gis_analysis_plugin_test)
endif()
