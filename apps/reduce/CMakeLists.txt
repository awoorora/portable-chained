add_executable(reduce main.cpp)
target_link_libraries(reduce PRIVATE vkdeps)
target_hlsl_shader(reduce reduce.hlsl)
project_warnings(reduce)