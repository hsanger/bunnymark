cmake_minimum_required(VERSION 4.0)
project(bunnymark)

set(CMAKE_CXX_STANDARD 20)

add_executable(bunnymark_bgfx src/bunnymark_bgfx.cpp shaders/bgfx/fs_bunny.sc shaders/bgfx/vs_bunny.sc shaders/bgfx/varying.def.sc)
add_executable(bunnymark_bgfx_simple src/bunnymark_bgfx_simple.cpp shaders/bgfx_simple/fs_bunny.sc shaders/bgfx_simple/vs_bunny.sc shaders/bgfx_simple/varying.def.sc)
add_executable(bunnymark_sdl2_gpu src/bunnymark_sdl2_gpu.cpp)
add_executable(bunnymark_sdl3_gpu src/bunnymark_sdl3_gpu.cpp)
add_executable(bunnymark_sdl_renderer src/bunnymark_sdl_renderer.cpp)

# required for SDL_gpu
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
find_package(OpenGL REQUIRED)

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL2 EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_gpu EXCLUDE_FROM_ALL)
add_subdirectory(vendored/bgfx EXCLUDE_FROM_ALL)

target_include_directories(bunnymark_bgfx PRIVATE shaders/bgfx)
target_include_directories(bunnymark_bgfx_simple PRIVATE shaders/bgfx_simple)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS shaders/bgfx/vs_bunny.sc
    VARYING_DEF ${CMAKE_SOURCE_DIR}/shaders/bgfx/varying.def.sc
    INCLUDE_DIRS ${BGFX_DIR}/src
    OUTPUT_DIR shaders/bgfx
)
bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS shaders/bgfx/fs_bunny.sc
    VARYING_DEF ${CMAKE_SOURCE_DIR}/shaders/bgfx/varying.def.sc
    INCLUDE_DIRS ${BGFX_DIR}/src
    OUTPUT_DIR shaders/bgfx
)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS shaders/bgfx_simple/vs_bunny.sc
    VARYING_DEF ${CMAKE_SOURCE_DIR}/shaders/bgfx_simple/varying.def.sc
    INCLUDE_DIRS ${BGFX_DIR}/src
    OUTPUT_DIR shaders/bgfx_simple
)
bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS shaders/bgfx_simple/fs_bunny.sc
    VARYING_DEF ${CMAKE_SOURCE_DIR}/shaders/bgfx_simple/varying.def.sc
    INCLUDE_DIRS ${BGFX_DIR}/src
    OUTPUT_DIR shaders/bgfx_simple
)

target_include_directories(bunnymark_sdl2_gpu PRIVATE vendored/SDL_gpu/include)

target_link_libraries(bunnymark_bgfx PRIVATE SDL3::SDL3 bx bgfx bimg_decode)
target_link_libraries(bunnymark_bgfx_simple PRIVATE SDL3::SDL3 bx bgfx bimg_decode)
target_link_libraries(bunnymark_sdl2_gpu PRIVATE SDL2::SDL2 OpenGL::GL SDL_gpu)
target_link_libraries(bunnymark_sdl3_gpu PRIVATE SDL3::SDL3)
target_link_libraries(bunnymark_sdl_renderer PRIVATE SDL3::SDL3)
