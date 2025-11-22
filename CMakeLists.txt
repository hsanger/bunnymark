cmake_minimum_required(VERSION 4.0)
project(sdl3_gpu_bunnymark)

set(CMAKE_CXX_STANDARD 20)

add_executable(sdl3_gpu_bunnymark main.cpp)

# SDL3
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
target_link_libraries(sdl3_gpu_bunnymark PRIVATE SDL3::SDL3)
