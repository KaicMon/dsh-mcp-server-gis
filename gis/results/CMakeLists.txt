add_library(gis_results
    src/memory_result_store.cpp
    src/geojson_output_policy.cpp
    src/result_publisher.cpp
)

target_include_directories(gis_results
    PUBLIC include
    PRIVATE ${CMAKE_SOURCE_DIR}/mcp_server/include
)
target_link_libraries(gis_results PUBLIC CURL::libcurl)
target_compile_features(gis_results PUBLIC cxx_std_20)
target_compile_options(gis_results PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(gis_results PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(gis_result_server server/result_server.cpp)
target_include_directories(gis_result_server PRIVATE
    ${CMAKE_SOURCE_DIR}/mcp_server/include
)
target_link_libraries(gis_result_server PRIVATE gis_results Threads::Threads)
target_compile_features(gis_result_server PRIVATE cxx_std_20)
target_compile_options(gis_result_server PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(gis_results_test test/memory_result_store_test.cpp)
    target_link_libraries(gis_results_test PRIVATE gis_results Threads::Threads)
    add_test(NAME gis_results COMMAND gis_results_test)

    add_executable(gis_output_policy_test test/geojson_output_policy_test.cpp)
    target_link_libraries(gis_output_policy_test PRIVATE gis_results)
    add_test(NAME gis_output_policy COMMAND gis_output_policy_test)

    add_test(NAME gis_result_server_http
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/result_server_smoke.sh
            $<TARGET_FILE:gis_result_server> 18088)
    add_test(NAME gis_map_services_external_result
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/map_services_external_smoke.sh
            $<TARGET_FILE:gis_result_server>
            $<TARGET_FILE:map_services_plugin_test> 18089)
    add_test(NAME gis_analysis_external_result
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/gis_analysis_external_smoke.sh
            $<TARGET_FILE:gis_result_server>
            $<TARGET_FILE:gis_analysis_plugin_test> 18090)
    add_test(NAME gis_analysis_inline_published_result
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/gis_analysis_external_smoke.sh
            $<TARGET_FILE:gis_result_server>
            $<TARGET_FILE:gis_analysis_plugin_test> 18095 inline-published)
endif()
