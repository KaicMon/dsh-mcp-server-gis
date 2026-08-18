find_package(OpenSSL REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)

add_library(gis_cache
    src/response_cache.cpp
    src/memory_response_cache.cpp
    src/redis_response_cache.cpp
)
target_include_directories(gis_cache PUBLIC include)
target_link_libraries(gis_cache PUBLIC OpenSSL::Crypto PkgConfig::HIREDIS)
target_compile_features(gis_cache PUBLIC cxx_std_20)
target_compile_options(gis_cache PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(gis_cache PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(gis_cache_test test/response_cache_test.cpp)
    target_link_libraries(gis_cache_test PRIVATE gis_cache)
    add_test(NAME gis_cache COMMAND gis_cache_test)

    add_executable(gis_redis_cache_test test/redis_cache_test.cpp)
    target_link_libraries(gis_redis_cache_test PRIVATE gis_cache)
    add_test(NAME gis_redis_cache
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/redis_cache_smoke.sh
            $<TARGET_FILE:gis_redis_cache_test> 16379)
endif()
