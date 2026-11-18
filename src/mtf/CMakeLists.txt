# CPU-side MTF code shared by the apps: test inputs, BWT via libsais, serial MTF.
# No Vulkan here, so it doesn't link vkdeps.
add_library(mtfcore STATIC
        inputs.cpp
        serial_mtf.cpp
        tiled_mtf.cpp
        depth_histogram.cpp
        word_encoding.h
)
target_include_directories(mtfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mtfcore PRIVATE libsais)

project_warnings(mtfcore)