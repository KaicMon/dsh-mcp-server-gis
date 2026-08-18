add_library(gis_providers
    src/amap_provider.cpp
    src/fallback_map_provider.cpp
    src/gcj02_coordinate_adapter.cpp
    src/http_client.cpp
)

target_include_directories(gis_providers PUBLIC include)
target_include_directories(gis_providers PRIVATE
    ${CMAKE_SOURCE_DIR}/mcp_server/include
)
target_link_libraries(gis_providers PUBLIC CURL::libcurl gis_cache)
target_compile_features(gis_providers PUBLIC cxx_std_20)
target_compile_options(gis_providers PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(gis_providers PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(TARGET routing_osm)
    target_sources(gis_providers PRIVATE src/local_osm_provider.cpp)
    target_link_libraries(gis_providers PUBLIC routing_osm)
    target_compile_definitions(gis_providers PUBLIC GIS_HAS_LOCAL_OSM_PROVIDER=1)
endif()

if(BUILD_TESTING)
    add_executable(gis_providers_test test/fallback_map_provider_test.cpp)
    target_link_libraries(gis_providers_test PRIVATE gis_providers)
    add_test(NAME gis_providers COMMAND gis_providers_test)

    add_executable(amap_provider_test test/amap_provider_test.cpp)
    target_link_libraries(amap_provider_test PRIVATE gis_providers)
    add_test(NAME gis_amap_provider COMMAND amap_provider_test)

    add_executable(gcj02_coordinate_adapter_test test/gcj02_coordinate_adapter_test.cpp)
    target_link_libraries(gcj02_coordinate_adapter_test PRIVATE gis_providers)
    add_test(NAME gis_gcj02_coordinate_adapter COMMAND gcj02_coordinate_adapter_test)

    add_executable(http_client_decorators_test test/http_client_decorators_test.cpp)
    target_link_libraries(http_client_decorators_test PRIVATE gis_providers)
    add_test(NAME gis_http_client_decorators COMMAND http_client_decorators_test)

    if(TARGET routing_osm)
        add_executable(local_osm_provider_test test/local_osm_provider_test.cpp)
        target_link_libraries(local_osm_provider_test PRIVATE gis_providers)
        target_compile_definitions(local_osm_provider_test PRIVATE
            ROUTING_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/routing/data/test"
        )
        add_test(NAME gis_local_osm_provider COMMAND local_osm_provider_test)
    endif()
endif()
