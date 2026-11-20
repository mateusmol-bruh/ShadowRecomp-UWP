add_subdirectory(${SHADOW_RECOMP_TOOLS_ROOT}/bc_diff)
# Retain XboxUtils for the runtime, without building the legacy CLI/analyzer.
add_subdirectory(${SHADOW_RECOMP_TOOLS_ROOT}/XboxRecomp EXCLUDE_FROM_ALL)
if(SHADOW_RECOMP_UWP)
    set_property(TARGET XboxUtils Zydis Zycore fmt xxhash PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
    set_property(SOURCE "${SHADOW_RECOMP_TOOLS_ROOT}/XboxRecomp/XboxUtils/memory_mapped_file.cpp"
        TARGET_DIRECTORY XboxUtils APPEND PROPERTY COMPILE_OPTIONS
        "/FI${CMAKE_SOURCE_DIR}/toolchains/uwp/xboxutils-file-api.h")
endif()
add_subdirectory(${SHADOW_RECOMP_TOOLS_ROOT}/XShaderRecomp)

if(SHADOW_RECOMP_UWP)
    if(NOT EXISTS "${SHADOW_RECOMP_HOST_FILE_TO_C}")
        message(FATAL_ERROR "Build the host file_to_c tool with toolchains/uwp/build.ps1.")
    endif()
    add_executable(file_to_c IMPORTED GLOBAL)
    set_property(TARGET file_to_c PROPERTY IMPORTED_LOCATION "${SHADOW_RECOMP_HOST_FILE_TO_C}")
else()
    add_subdirectory(${SHADOW_RECOMP_TOOLS_ROOT}/file_to_c)
endif()
