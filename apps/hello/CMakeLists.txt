add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE vkdeps)
target_hlsl_shader(hello hello.hlsl)
project_warnings(hello)
