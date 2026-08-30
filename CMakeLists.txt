cmake_minimum_required(VERSION 3.16)
project(oucs VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Options ────────────────────────────────────────────────────
option(OUCS_BUILD_CLI      "Build oucs CLI tool"           ON)
option(OUCS_BUILD_TESTS    "Build test suite"              ON)
option(OUCS_USE_LIBSODIUM  "Use libsodium for crypto"      OFF)
option(OUCS_USE_TLS        "Enable HTTPS (requires mbedTLS)" OFF)
option(OUCS_BUILD_WASM     "Build WebAssembly target"      OFF)

# ── Compiler flags ─────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    else()
        add_compile_options(-g -O0 -fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ── Core library source files ──────────────────────────────────
set(OUCS_CORE_SOURCES
    src/oucs_encoder.c
    src/oucs_decoder.c
    src/oucs_stream.c
    src/oucs_ecc.c
    src/oucs_crypto.c
    src/oucs_analysis.c
    src/oucs_delta.c
    src/oucs_history.c
    src/oucs_hooks.c
    src/oucs_network.c
    src/oucs_util.c
)

# ── liboucs shared library ─────────────────────────────────────
add_library(oucs_shared SHARED ${OUCS_CORE_SOURCES})
target_include_directories(oucs_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(oucs_shared PROPERTIES
    OUTPUT_NAME  oucs
    VERSION      ${PROJECT_VERSION}
    SOVERSION    1
    PUBLIC_HEADER include/oucs_format.h
)

# ── liboucs static library ─────────────────────────────────────
add_library(oucs_static STATIC ${OUCS_CORE_SOURCES})
target_include_directories(oucs_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(oucs_static PROPERTIES OUTPUT_NAME oucs_static)

# Math library (needed for oucs_analysis.c)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(oucs_shared PRIVATE ${MATH_LIBRARY})
    target_link_libraries(oucs_static PRIVATE ${MATH_LIBRARY})
endif()

# ── Optional: libsodium crypto backend ─────────────────────────
if(OUCS_USE_LIBSODIUM)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED libsodium)
    target_compile_definitions(oucs_shared PRIVATE OUCS_USE_LIBSODIUM=1)
    target_compile_definitions(oucs_static PRIVATE OUCS_USE_LIBSODIUM=1)
    target_link_libraries(oucs_shared PRIVATE ${SODIUM_LIBRARIES})
    target_link_libraries(oucs_static PRIVATE ${SODIUM_LIBRARIES})
    target_include_directories(oucs_shared PRIVATE ${SODIUM_INCLUDE_DIRS})
    target_include_directories(oucs_static PRIVATE ${SODIUM_INCLUDE_DIRS})
    message(STATUS "Using libsodium for crypto")
endif()

# ── Optional: mbedTLS for HTTPS ────────────────────────────────
if(OUCS_USE_TLS)
    find_package(MbedTLS QUIET)
    if(MbedTLS_FOUND)
        target_compile_definitions(oucs_shared PRIVATE OUCS_USE_TLS=1)
        target_compile_definitions(oucs_static PRIVATE OUCS_USE_TLS=1)
        target_link_libraries(oucs_shared PRIVATE MbedTLS::mbedtls)
        target_link_libraries(oucs_static PRIVATE MbedTLS::mbedtls)
        message(STATUS "Using mbedTLS for HTTPS support")
    else()
        message(WARNING "OUCS_USE_TLS=ON but MbedTLS not found — HTTPS disabled")
    endif()
endif()

# ── CLI binary ─────────────────────────────────────────────────
if(OUCS_BUILD_CLI)
    add_executable(oucs cli/oucs_cli.c)
    target_link_libraries(oucs PRIVATE oucs_static)
    target_include_directories(oucs PRIVATE include)
    if(MATH_LIBRARY)
        target_link_libraries(oucs PRIVATE ${MATH_LIBRARY})
    endif()
    install(TARGETS oucs RUNTIME DESTINATION bin)
endif()

# ── WebAssembly target ─────────────────────────────────────────
if(OUCS_BUILD_WASM)
    add_executable(oucs_wasm ${OUCS_CORE_SOURCES})
    target_include_directories(oucs_wasm PRIVATE include)
    set_target_properties(oucs_wasm PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "-s WASM=1 -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
                    -s EXPORTED_FUNCTIONS=['_oucs_writer_create','_oucs_writer_add_song_mem',\
                    '_oucs_writer_finalize','_oucs_writer_free',\
                    '_oucs_reader_open','_oucs_reader_song_count',\
                    '_oucs_reader_get_song_info','_oucs_reader_extract_song_mem',\
                    '_oucs_reader_free','_oucs_stream_open','_oucs_stream_read_chunk',\
                    '_oucs_stream_seek','_oucs_stream_free',\
                    '_oucs_version','_oucs_strerror','_malloc','_free'] \
                    -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME=OucsModule"
    )
    if(MATH_LIBRARY)
        target_link_libraries(oucs_wasm PRIVATE ${MATH_LIBRARY})
    endif()
endif()

# ── Tests ──────────────────────────────────────────────────────
if(OUCS_BUILD_TESTS)
    enable_testing()
    add_executable(oucs_test tests/test_oucs.c)
    target_link_libraries(oucs_test PRIVATE oucs_static)
    target_include_directories(oucs_test PRIVATE include)
    if(MATH_LIBRARY)
        target_link_libraries(oucs_test PRIVATE ${MATH_LIBRARY})
    endif()
    add_test(NAME oucs_core_tests COMMAND oucs_test)
endif()

# ── Install rules ─────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS oucs_shared oucs_static
    EXPORT OucsTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT OucsTargets
    FILE OucsTargets.cmake
    NAMESPACE Oucs::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/oucs
)

# Package config
include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OucsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OucsConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/oucs
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OucsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OucsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OucsConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/oucs
)

# ── Summary ────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "OUCS Engine v${PROJECT_VERSION} — Build Configuration:")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  CLI tool       : ${OUCS_BUILD_CLI}")
message(STATUS "  Tests          : ${OUCS_BUILD_TESTS}")
message(STATUS "  libsodium      : ${OUCS_USE_LIBSODIUM}")
message(STATUS "  TLS/HTTPS      : ${OUCS_USE_TLS}")
message(STATUS "  WASM           : ${OUCS_BUILD_WASM}")
message(STATUS "")
