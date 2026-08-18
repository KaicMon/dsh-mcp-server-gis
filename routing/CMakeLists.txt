find_package(BZip2 REQUIRED)
find_package(EXPAT REQUIRED)
find_package(ZLIB REQUIRED)

# Header-only libosmium still needs these compression/XML libraries when the
# reader handles PBF and XML inputs.
add_library(routing_osm
    builder/osm_parser.cpp
    builder/topology_builder.cpp
    builder/csr_builder.cpp
    core/src/router.cpp
    core/src/spatial_index.cpp
    core/src/network_store.cpp
    core/src/routing_engine.cpp
    core/src/map_matcher.cpp
    storage/network_file.cpp
)

target_include_directories(routing_osm
    PUBLIC core/include
)

target_link_libraries(routing_osm
    PUBLIC
        BZip2::BZip2
        EXPAT::EXPAT
        Threads::Threads
        ZLIB::ZLIB
)

target_compile_features(routing_osm PUBLIC cxx_std_20)
target_compile_options(routing_osm PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(routing_osm PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(osm_inspect tools/osm_inspect.cpp)
target_link_libraries(osm_inspect PRIVATE routing_osm)

add_executable(topology_inspect tools/topology_inspect.cpp)
target_link_libraries(topology_inspect PRIVATE routing_osm)

add_executable(osm_build tools/osm_build.cpp)
target_link_libraries(osm_build PRIVATE routing_osm)

add_executable(route_benchmark benchmark/route_benchmark.cpp)
target_link_libraries(route_benchmark PRIVATE routing_osm)

add_executable(routing_http http/routing_http_service.cpp)
target_link_libraries(routing_http PRIVATE routing_osm Threads::Threads)
target_include_directories(routing_http PRIVATE ${CMAKE_SOURCE_DIR}/mcp_server/include)

if(BUILD_TESTING)
    add_executable(osm_parser_test test/osm_parser_test.cpp)
    target_link_libraries(osm_parser_test PRIVATE routing_osm)
    target_compile_definitions(osm_parser_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_osm_parser COMMAND osm_parser_test)

    add_executable(topology_builder_test test/topology_builder_test.cpp)
    target_link_libraries(topology_builder_test PRIVATE routing_osm)
    target_compile_definitions(topology_builder_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_topology_builder COMMAND topology_builder_test)

    add_executable(csr_graph_test test/csr_graph_test.cpp)
    target_link_libraries(csr_graph_test PRIVATE routing_osm)
    target_compile_definitions(csr_graph_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_csr_graph COMMAND csr_graph_test)

    add_executable(router_test test/router_test.cpp)
    target_link_libraries(router_test PRIVATE routing_osm)
    target_compile_definitions(router_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_router COMMAND router_test)

    add_executable(spatial_index_test test/spatial_index_test.cpp)
    target_link_libraries(spatial_index_test PRIVATE routing_osm)
    target_compile_definitions(spatial_index_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_spatial_index COMMAND spatial_index_test)

    add_executable(network_store_test test/network_store_test.cpp)
    target_link_libraries(network_store_test PRIVATE routing_osm)
    target_compile_definitions(network_store_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_network_store COMMAND network_store_test)

    add_executable(routing_engine_test test/routing_engine_test.cpp)
    target_link_libraries(routing_engine_test PRIVATE routing_osm)
    target_compile_definitions(routing_engine_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_engine COMMAND routing_engine_test)

    add_executable(map_matcher_test test/map_matcher_test.cpp)
    target_link_libraries(map_matcher_test PRIVATE routing_osm)
    target_compile_definitions(map_matcher_test PRIVATE
        ROUTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/test"
    )
    add_test(NAME routing_map_matcher COMMAND map_matcher_test)
endif()
