cmake_minimum_required(VERSION 3.25)
project(portable_chained LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All executables land in build/<preset>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Static C++ runtime on Windows: the .exe runs on a friend's PC without
# installing the Visual C++ redistributable. Must be set before any target.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Vulkan headers and DXC.
# We never link the Vulkan loader directly: volk loads it at runtime, so we
# only need the headers from the SDK.
find_package(Vulkan REQUIRED)

# Look in the Vulkan SDK before PATH. The MSVC environment puts the Windows
# SDK's own dxc.exe on PATH, and that build cannot emit SPIR-V.
find_program(DXC_EXECUTABLE NAMES dxc
  HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
  REQUIRED)

# Fail early, with a clear message, if this dxc cannot produce SPIR-V.
file(WRITE "${CMAKE_BINARY_DIR}/dxc_check.hlsl"
  "[numthreads(1, 1, 1)] void main() {}\n")
execute_process(
  COMMAND "${DXC_EXECUTABLE}" -spirv -T cs_6_0 -E main
          -Fo "${CMAKE_BINARY_DIR}/dxc_check.spv" "${CMAKE_BINARY_DIR}/dxc_check.hlsl"
  RESULT_VARIABLE dxc_result OUTPUT_QUIET ERROR_VARIABLE dxc_error)
if(NOT dxc_result EQUAL 0)
  message(FATAL_ERROR "${DXC_EXECUTABLE} cannot compile to SPIR-V. "
                      "Is it the Vulkan SDK's dxc?\n${dxc_error}")
endif()
message(STATUS "DXC used for shaders: ${DXC_EXECUTABLE}")

# ---------------------------------------------------------------------------
# volk (function loader) and VMA (memory allocator), pinned by version and
# hash so every machine builds exactly the same code.
include(FetchContent)
FetchContent_Declare(volk
  URL      https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
  URL_HASH SHA256=1547d8d74395d4048fb3f4a6313da56db626e89b55db238d0d7e8944c8a645f3
)
FetchContent_Declare(vma
  URL      https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.tar.gz
  URL_HASH SHA256=822aa850c6ce77346ae96a8a1d351d52e77e85929f35363849a0a4e638e0a2a1
)
FetchContent_MakeAvailable(volk vma)

# One target that bundles volk + VMA, including VMA's single implementation
# file. Every app links this instead of linking volk and VMA separately.
add_library(vkdeps STATIC src/vma_impl.cpp)
target_link_libraries(vkdeps PUBLIC volk::volk GPUOpen::VulkanMemoryAllocator)

# ---------------------------------------------------------------------------
# Helpers used by the apps.

# Warnings for our own code only (third-party code stays quiet).
function(project_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# Compiles an HLSL compute shader to SPIR-V with DXC and embeds it in the
# program as a byte array, so the .exe needs no shader files next to it:
#   foo.hlsl  ->  <build>/.../shaders/foo_spv.h  defining  g_foo_spv[]
# Editing the .hlsl file triggers a rebuild automatically.
function(target_hlsl_shader target hlsl_file)
  get_filename_component(name "${hlsl_file}" NAME_WE)
  get_filename_component(src  "${hlsl_file}" ABSOLUTE)
  set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(header  "${out_dir}/${name}_spv.h")

  add_custom_command(
    OUTPUT  "${header}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
    COMMAND "${DXC_EXECUTABLE}"
            -spirv -T cs_6_0 -E main
            -fspv-target-env=vulkan1.3
            -Fh "${header}" -Vn "g_${name}_spv"
            "${src}"
    DEPENDS "${src}"
    COMMENT "DXC: ${name}.hlsl -> SPIR-V"
    VERBATIM
  )
  target_sources(${target} PRIVATE "${header}")
  target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()

# ---------------------------------------------------------------------------
add_subdirectory(apps/hello)
add_subdirectory(apps/reduce)