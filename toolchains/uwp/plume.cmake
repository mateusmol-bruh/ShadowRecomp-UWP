set(plume_source "${SHADOW_RECOMP_THIRDPARTY_ROOT}/plume")
set(plume_port "${CMAKE_CURRENT_BINARY_DIR}/plume-uwp")
set(plume_patch "${CMAKE_CURRENT_LIST_DIR}/plume-corewindow.patch")
find_package(Git REQUIRED)

# Apply the CoreWindow variant in the build tree so desktop uses upstream Plume.
file(MAKE_DIRECTORY "${plume_port}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${plume_source}/plume_d3d12.cpp")
file(READ "${plume_source}/plume_d3d12.cpp" plume_original)
file(WRITE "${plume_port}/plume_d3d12.cpp" "${plume_original}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E chdir "${plume_port}"
    "${GIT_EXECUTABLE}" -c core.autocrlf=false apply --no-index --ignore-space-change "${plume_patch}"
    COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${plume_patch}")

add_library(plume STATIC
    "${plume_port}/plume_d3d12.cpp"
    "${plume_source}/contrib/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp"
)
target_include_directories(plume PRIVATE "${CMAKE_SOURCE_DIR}/ShadowRecomp")
target_include_directories(plume PUBLIC
    "${plume_source}"
    "${plume_source}/contrib/volk"
    "${plume_source}/contrib/Vulkan-Headers/include"
    "${plume_source}/contrib/VulkanMemoryAllocator/include"
    "${plume_source}/contrib/D3D12MemoryAllocator/include"
)
target_compile_definitions(plume PUBLIC NOMINMAX)
target_link_libraries(plume PRIVATE d3d12 dxgi windowsapp)
