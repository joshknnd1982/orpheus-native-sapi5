cmake_minimum_required(VERSION 3.15)
project(OrpheusNativeSAPI VERSION 1.1.0 LANGUAGES C CXX)

# Keep this version, installer/orpheus_native.iss's MyAppVersion and the
# README in step.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BUILD_X64 TRUE)
    message(STATUS "Building x64 version")
else()
    set(BUILD_X64 FALSE)
    message(STATUS "Building x86 version")
endif()

set(COMMON_DEFS _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)

# Shared client for the Orpheus engine host, the settings store, the engine's
# voice table and logging.  Identical for both architectures: the host is
# always the shipped 32-bit orpheus-native-host.exe, reached over a loopback
# socket, so the 64-bit build needs no bridge process of its own.
add_library(orpheus_core STATIC
    src/orpheus_client.cpp
    src/settings.cpp
    src/voicedesc.cpp
    src/installed_voices.cpp
    src/debug_log.cpp
)

target_include_directories(orpheus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(orpheus_core PUBLIC ws2_32 shlwapi advapi32 shell32)
target_compile_definitions(orpheus_core PRIVATE ${COMMON_DEFS})

# The SAPI5 engine DLL.
add_library(OrpheusNativeSAPI SHARED
    src/sapi_main.cpp
    src/com.cpp
    src/registry.cpp
    src/sapi_registration.cpp
    src/ISpDataKeyImpl.cpp
    src/IEnumSpObjectTokensImpl.cpp
    src/ISpTTSEngineImpl.cpp
    src/voice_token.cpp
    src/version.rc
    src/orpheus_sapi.def
)

target_include_directories(OrpheusNativeSAPI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(OrpheusNativeSAPI PRIVATE orpheus_core ole32 oleaut32 advapi32 shell32)
target_compile_definitions(OrpheusNativeSAPI PRIVATE ${COMMON_DEFS})

# Developer-only harnesses; not installed.

# Drives register_token_enumerator and remove_token_enumerator themselves,
# against a sandbox key under HKCU.  The SAPI harness registers and
# unregisters with helpers of its own, so without this the product's uninstall
# path would never actually be run by a test - which is exactly how a
# uninstall that silently removed nothing shipped in a sibling project.
add_executable(registry_test
    test/registry_test.cpp
    src/registry.cpp
    src/sapi_registration.cpp
)
target_include_directories(registry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(registry_test PRIVATE advapi32)
target_compile_definitions(registry_test PRIVATE ${COMMON_DEFS})

# Exercises the real SAPI5 stack end to end.
add_executable(sapi_test test/sapi_test.cpp)
target_include_directories(sapi_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sapi_test PRIVATE ole32 sapi)
target_compile_definitions(sapi_test PRIVATE ${COMMON_DEFS})

# Reads the live configuration dialog back through MSAA: every control must be
# a tab stop and must report a name.
if(NOT BUILD_X64)
    add_executable(a11y_probe test/a11y_probe.cpp)
    target_include_directories(a11y_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(a11y_probe PRIVATE oleacc ole32 user32)
    target_compile_definitions(a11y_probe PRIVATE ${COMMON_DEFS})
endif()

# The configuration utility only needs to exist once; build it in the x86 pass
# so it runs on every Windows installation.
if(NOT BUILD_X64)
    add_executable(OrpheusNativeConfig WIN32
        src/config_tool.cpp
        src/orpheus_config.rc
        src/version_config.rc
    )
    target_include_directories(OrpheusNativeConfig PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(OrpheusNativeConfig PRIVATE orpheus_core comctl32 ole32)
    target_compile_definitions(OrpheusNativeConfig PRIVATE ${COMMON_DEFS})
endif()

if(MSVC)
    foreach(tgt orpheus_core OrpheusNativeSAPI registry_test sapi_test a11y_probe
                OrpheusNativeConfig)
        if(TARGET ${tgt})
            target_compile_options(${tgt} PRIVATE /W3 /MP /utf-8)
            set_property(TARGET ${tgt} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    endforeach()
endif()
