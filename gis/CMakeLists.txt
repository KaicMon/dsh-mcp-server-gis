find_package(GDAL REQUIRED)

add_library(gis_core
    core/src/coordinate_transformer.cpp
    core/src/gdal_runtime.cpp
    core/src/geometry_engine.cpp
    core/src/vector_dataset_engine.cpp
)

target_include_directories(gis_core
    PUBLIC core/include
    # GeoJSON serialization uses the project's pinned single-header JSON
    # implementation instead of depending on a distribution-specific package.
    PRIVATE ${CMAKE_SOURCE_DIR}/mcp_server/include
)

# Modern FindGDAL provides GDAL::GDAL. Keeping the fallback makes the project
# usable with older CMake/GDAL packages without leaking those details outward.
if(TARGET GDAL::GDAL)
    target_link_libraries(gis_core PUBLIC GDAL::GDAL)
else()
    target_include_directories(gis_core SYSTEM PUBLIC ${GDAL_INCLUDE_DIRS})
    target_link_libraries(gis_core PUBLIC ${GDAL_LIBRARIES})
endif()

target_compile_features(gis_core PUBLIC cxx_std_20)
target_compile_options(gis_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(gis_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(gis_core_smoke_test test/gis_core_smoke_test.cpp)
    target_link_libraries(gis_core_smoke_test PRIVATE gis_core)
    add_test(NAME gis_core_smoke COMMAND gis_core_smoke_test)
endif()

add_subdirectory(cache)
add_subdirectory(providers)
add_subdirectory(results)
