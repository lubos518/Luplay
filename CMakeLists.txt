cmake_minimum_required(VERSION 3.20)

project(Luplay 
    VERSION 0.1.0 
    DESCRIPTION "Hardware-accelerated C++ video player with on-demand AI transcription"
    LANGUAGES C CXX
)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add custom CMake module directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Fix CUDA 13.3 + MSVC traditional preprocessor error properly
if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")
    add_compile_options("$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor>")
endif()

# Include FetchContent for dependencies
include(FetchContent)

# -----------------------------------------------------------------------------
# Dependency Discovery
# -----------------------------------------------------------------------------

# 1. GLFW 3
find_package(glfw3 REQUIRED)

# 2. OpenGL
find_package(OpenGL REQUIRED)

# 3. libmpv (Video Playback Engine)
find_package(MPV REQUIRED)

# 4. FFmpeg (Audio Demuxing & Resampling)
find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec swresample avutil)

# 5. whisper.cpp (AI Transcription Engine with OpenMP AVX2 Multi-threading)
if (NOT TARGET whisper AND NOT TARGET whisper.cpp)
    message(STATUS "Configuring whisper.cpp with OpenMP AVX2 SIMD Multi-threading...")
    if (APPLE)
        set(GGML_METAL ON CACHE BOOL "" FORCE)
        set(GGML_CUDA OFF CACHE BOOL "" FORCE)
    else()
        set(GGML_CUDA ON CACHE BOOL "" FORCE)
        set(GGML_METAL OFF CACHE BOOL "" FORCE)
    endif()
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        whisper_cpp
        GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
        GIT_TAG        v1.7.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(whisper_cpp)
endif()

# 6. Dear ImGui (GUI Toolkit)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# Create an ImGui target including GLFW + OpenGL3 backends
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    OpenGL::GL
)

# -----------------------------------------------------------------------------
# Target Definition: Luplay Executable
# -----------------------------------------------------------------------------

file(GLOB_RECURSE LUPLAY_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(luplay ${LUPLAY_SOURCES})

target_include_directories(luplay PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    ${MPV_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(luplay PRIVATE
    imgui
    glfw
    OpenGL::GL
    mpv::mpv
    ${FFMPEG_LIBRARIES}
)

if (TARGET whisper)
    add_dependencies(luplay whisper)
    target_include_directories(luplay PRIVATE ${whisper_cpp_SOURCE_DIR}/include ${whisper_cpp_SOURCE_DIR}/ggml/include ${whisper_cpp_SOURCE_DIR})
elseif (TARGET whisper.cpp)
    add_dependencies(luplay whisper.cpp)
    target_include_directories(luplay PRIVATE ${whisper_cpp_SOURCE_DIR}/include ${whisper_cpp_SOURCE_DIR}/ggml/include ${whisper_cpp_SOURCE_DIR})
endif()

# Compiler Warning Flags
if (MSVC)
    target_compile_options(luplay PRIVATE /W4 /utf-8)
else()
    target_compile_options(luplay PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set Output Directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Copy libmpv DLL and whisper/ggml DLLs to output directory on Windows
if (WIN32)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libmpv/libmpv-2.dll")
        add_custom_command(TARGET luplay POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/libmpv/libmpv-2.dll"
            "$<TARGET_FILE_DIR:luplay>/libmpv-2.dll"
        )
    endif()
    if (TARGET whisper)
        add_custom_command(TARGET luplay POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:whisper>
            $<TARGET_FILE:ggml>
            $<TARGET_FILE:ggml-base>
            $<TARGET_FILE:ggml-cpu>
            "$<TARGET_FILE_DIR:luplay>"
        )
    endif()
endif()

