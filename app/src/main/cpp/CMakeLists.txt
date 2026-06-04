cmake_minimum_required(VERSION 3.22.1)
project(asteroids LANGUAGES C CXX)

# --- Locate glslc (ships inside the NDK) and compile GLSL -> SPIR-V C arrays ---
file(GLOB GLSLC_CANDIDATES "${ANDROID_NDK}/shader-tools/*/glslc")
list(GET GLSLC_CANDIDATES 0 GLSLC)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found under ${ANDROID_NDK}/shader-tools")
endif()

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

function(compile_shader STAGE_FILE OUT_HEADER)
    add_custom_command(
        OUTPUT "${GEN_DIR}/${OUT_HEADER}"
        COMMAND "${GLSLC}" -mfmt=c -O
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${STAGE_FILE}"
                -o "${GEN_DIR}/${OUT_HEADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${STAGE_FILE}"
        COMMENT "Compiling ${STAGE_FILE} -> ${OUT_HEADER}"
        VERBATIM)
endfunction()

compile_shader(shape.vert shape_vert.spv.h)
compile_shader(shape.frag shape_frag.spv.h)
add_custom_target(shaders DEPENDS
    "${GEN_DIR}/shape_vert.spv.h"
    "${GEN_DIR}/shape_frag.spv.h")

# --- native_app_glue (lifecycle + input plumbing for NativeActivity) ---
add_library(native_app_glue STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC
    "${ANDROID_NDK}/sources/android/native_app_glue")

# --- The game library (loaded by NativeActivity as libasteroids.so) ---
add_library(asteroids SHARED
    main.cpp
    vk_renderer.cpp
    game.cpp)

add_dependencies(asteroids shaders)
target_include_directories(asteroids PRIVATE "${GEN_DIR}")

# Keep ANativeActivity_onCreate (provided by glue) from being stripped.
target_link_options(asteroids PRIVATE "-u" "ANativeActivity_onCreate")

target_link_libraries(asteroids
    native_app_glue
    vulkan
    android
    log)
