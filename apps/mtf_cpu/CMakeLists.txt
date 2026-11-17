add_executable(mtf_cpu main.cpp)
target_link_libraries(mtf_cpu PRIVATE mtfcore)

project_warnings(mtf_cpu)