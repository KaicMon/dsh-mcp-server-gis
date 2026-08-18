set(TEST_PLUGIN_ROOT "${CMAKE_CURRENT_BINARY_DIR}/plugins")

function(add_loader_test_plugin target source output_dir)
    add_library(${target} SHARED ${source})
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/interface
    )
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${TEST_PLUGIN_ROOT}/${output_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${TEST_PLUGIN_ROOT}/${output_dir}"
    )
endfunction()

add_loader_test_plugin(loader_valid_plugin
    plugins/LoaderValidPlugin.cpp valid)
add_loader_test_plugin(loader_init_failure_plugin
    plugins/LoaderInitFailurePlugin.cpp init-failure)
add_loader_test_plugin(loader_missing_exports_plugin
    plugins/LoaderMissingExportsPlugin.cpp missing-exports)
add_loader_test_plugin(loader_null_instance_plugin
    plugins/LoaderNullInstancePlugin.cpp null-instance)
add_loader_test_plugin(loader_incompatible_abi_plugin
    plugins/LoaderIncompatibleAbiPlugin.cpp incompatible-abi)
add_loader_test_plugin(loader_small_api_plugin
    plugins/LoaderSmallApiPlugin.cpp small-api)
add_loader_test_plugin(loader_invalid_schema_plugin
    plugins/LoaderInvalidSchemaPlugin.cpp invalid-schema)
add_loader_test_plugin(loader_duplicate_tool_a_plugin
    plugins/LoaderDuplicateToolAPlugin.cpp duplicate-tools)
add_loader_test_plugin(loader_duplicate_tool_b_plugin
    plugins/LoaderDuplicateToolBPlugin.cpp duplicate-tools)
add_loader_test_plugin(loader_concurrent_plugin
    plugins/LoaderConcurrentPlugin.cpp concurrent)
target_link_libraries(loader_concurrent_plugin PRIVATE Threads::Threads)
add_loader_test_plugin(runtime_plugin_v1
    plugins/RuntimePluginV1.cpp runtime-v1)
add_loader_test_plugin(runtime_plugin_v2
    plugins/RuntimePluginV2.cpp runtime-v2)

add_executable(plugins_loader_test
    plugins_loader_test.cpp
    ${PROJECT_SOURCE_DIR}/src/loader/PluginsLoader.cpp
)

target_include_directories(plugins_loader_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/loader
    ${PROJECT_SOURCE_DIR}/src/interface
    ${PROJECT_SOURCE_DIR}/libs_tier_01/aixlog-1.5.0/include
)

target_link_libraries(plugins_loader_test PRIVATE ${CMAKE_DL_LIBS})

add_test(
    NAME plugins_loader_lifecycle
    COMMAND plugins_loader_test
        "$<TARGET_FILE_DIR:loader_valid_plugin>"
        "$<TARGET_FILE_DIR:loader_init_failure_plugin>"
        "$<TARGET_FILE_DIR:loader_missing_exports_plugin>"
        "$<TARGET_FILE_DIR:loader_null_instance_plugin>"
        "$<TARGET_FILE_DIR:loader_incompatible_abi_plugin>"
        "$<TARGET_FILE_DIR:loader_small_api_plugin>"
        "${CMAKE_CURRENT_BINARY_DIR}/lifecycle-events.log"
)

set_tests_properties(plugins_loader_lifecycle PROPERTIES
    TIMEOUT 15
)

add_executable(plugin_registry_test
    plugin_registry_test.cpp
    ${PROJECT_SOURCE_DIR}/src/loader/PluginsLoader.cpp
    ${PROJECT_SOURCE_DIR}/src/loader/PluginRegistry.cpp
)

target_include_directories(plugin_registry_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/loader
    ${PROJECT_SOURCE_DIR}/src/interface
    ${PROJECT_SOURCE_DIR}/libs_tier_01/aixlog-1.5.0/include
)

target_link_libraries(plugin_registry_test PRIVATE
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

add_test(
    NAME plugin_registry_staging
    COMMAND plugin_registry_test
        "$<TARGET_FILE_DIR:loader_valid_plugin>"
        "$<TARGET_FILE_DIR:loader_invalid_schema_plugin>"
        "$<TARGET_FILE_DIR:loader_duplicate_tool_a_plugin>"
)

set_tests_properties(plugin_registry_staging PROPERTIES TIMEOUT 15)

add_executable(plugin_runtime_test
    plugin_runtime_test.cpp
    ${PROJECT_SOURCE_DIR}/src/loader/PluginsLoader.cpp
    ${PROJECT_SOURCE_DIR}/src/loader/PluginRegistry.cpp
    ${PROJECT_SOURCE_DIR}/src/runtime/PluginRuntime.cpp
    ${PROJECT_SOURCE_DIR}/src/runtime/PluginWatcher.cpp
)
target_include_directories(plugin_runtime_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/loader
    ${PROJECT_SOURCE_DIR}/src/runtime
    ${PROJECT_SOURCE_DIR}/src/interface
    ${PROJECT_SOURCE_DIR}/libs_tier_01/aixlog-1.5.0/include
)
target_link_libraries(plugin_runtime_test PRIVATE
    ${CMAKE_DL_LIBS}
    Threads::Threads
    OpenSSL::Crypto
)
add_test(
    NAME plugin_runtime_reload
    COMMAND plugin_runtime_test
        "$<TARGET_FILE:runtime_plugin_v1>"
        "$<TARGET_FILE:runtime_plugin_v2>"
        "$<TARGET_FILE:loader_invalid_schema_plugin>"
        "$<TARGET_FILE:loader_valid_plugin>"
        "${CMAKE_CURRENT_BINARY_DIR}/runtime-work"
)
set_tests_properties(plugin_runtime_reload PROPERTIES TIMEOUT 20)
add_executable(tool_profile_test
    tool_profile_test.cpp
    ../src/tools/ToolProfile.cpp
)
target_include_directories(tool_profile_test PRIVATE
    ../include ../src
)
target_compile_features(tool_profile_test PRIVATE cxx_std_20)
target_compile_definitions(tool_profile_test PRIVATE
    TOOL_PROFILE_FILE="${CMAKE_SOURCE_DIR}/config/tool-profiles.json"
)
add_test(NAME tool_profile COMMAND tool_profile_test)

add_test(NAME tool_profile_stdio
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tool_profile_stdio_test.sh
        $<TARGET_FILE:mcp_server>
        ${CMAKE_SOURCE_DIR}/config/tool-profiles.json
        $<TARGET_FILE:gis-analysis>
        $<TARGET_FILE:map-services>
        $<TARGET_FILE:calculator>
)
