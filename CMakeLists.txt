cmake_minimum_required(VERSION 3.16)

set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "Vcpkg target triplet")
endif()

project(NeuTracingRender VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# Find Packages via vcpkg
find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# Shader Compilation
find_program(GLSLC_EXE NAMES glslc HINTS "C:/VulkanSDK/1.4.335.0/Bin" ${Vulkan_BIN_DIR})

if(GLSLC_EXE)
    message(STATUS "Found glslc: ${GLSLC_EXE}")
    set(SHADER_NAMES
        pathtrace.comp
        postprocess.comp
        bloom_threshold.comp
        bloom_downsample.comp
        bloom_upsample.comp
        denoise.comp
    )

    set(ALL_SPV_SHADERS "")
    foreach(sname ${SHADER_NAMES})
        set(SRC_S "${CMAKE_SOURCE_DIR}/resource/shaders/glsl/${sname}")
        set(DST_S "${CMAKE_SOURCE_DIR}/resource/shaders/compiled/${sname}.spv")
        add_custom_command(
            OUTPUT ${DST_S}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/resource/shaders/compiled"
            COMMAND ${GLSLC_EXE} -I "${CMAKE_SOURCE_DIR}/resource/shaders/glsl" ${SRC_S} -o ${DST_S}
            DEPENDS ${SRC_S} "${CMAKE_SOURCE_DIR}/resource/shaders/glsl/nishita_sky.glsl"
            COMMENT "Compiling shader ${sname} -> ${sname}.spv"
        )
        list(APPEND ALL_SPV_SHADERS ${DST_S})
    endforeach()

    add_custom_target(CompileShaders ALL DEPENDS ${ALL_SPV_SHADERS})
else()
    message(WARNING "glslc not found! Shaders must be pre-compiled.")
endif()

# Source files
set(SOURCES
    source/neuLog.cpp
    source/Window.cpp
    source/Camera.cpp
    source/BVH.cpp
    source/MaterialManager.cpp
    source/TextureManager.cpp
    source/Scene.cpp
    source/PathTracerCore.cpp
    source/FileBrowser.cpp
    source/neuGUI.cpp
    source/Console.cpp
    source/imgui_impl_sdl3.cpp
    source/imgui_impl_vulkan.cpp
    source/Main.cpp
)

add_executable(NeuTracingRender ${SOURCES})

if(TARGET CompileShaders)
    add_dependencies(NeuTracingRender CompileShaders)
endif()

target_include_directories(NeuTracingRender PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/ThirParty/stb"
    "${CMAKE_SOURCE_DIR}/ThirParty/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/ThirParty/nlohmann-json"
)

target_link_libraries(NeuTracingRender PRIVATE
    imgui::imgui
    SDL3::SDL3
    Vulkan::Vulkan
    spdlog::spdlog
    fmt::fmt
    glm::glm
)

# Copy resource folder to binary directory
add_custom_command(TARGET NeuTracingRender POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    "${CMAKE_SOURCE_DIR}/resource"
    "$<TARGET_FILE_DIR:NeuTracingRender>/resource"
    COMMENT "Copying resource directory to build output"
)
