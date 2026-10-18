# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ssf2t_hd_remix VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 unpacks the pinned SDK.
if(WIN32)
    set(SSF2T_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
else()
    set(SSF2T_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/linux-amd64")
endif()
list(APPEND CMAKE_PREFIX_PATH "${SSF2T_SDK_DIR}")

add_compile_options("$<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")

include(generated/rexglue.cmake)

# TRG Launcher: third_party/trg-launcher, or a local checkout named by SSF2T_TRG_LAUNCHER_DIR.
set(SSF2T_TRG_LAUNCHER_DIR "$ENV{SSF2T_TRG_LAUNCHER_DIR}" CACHE PATH
    "Local TRG Launcher checkout, used when third_party/trg-launcher is empty")
set(_trg_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/trg-launcher")
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    set(_trg_dir "${SSF2T_TRG_LAUNCHER_DIR}")
endif()
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    message(FATAL_ERROR "TRG Launcher not found: place it in third_party/trg-launcher or set SSF2T_TRG_LAUNCHER_DIR")
endif()

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use the ImGui compiled into rexruntime.
add_library(ssf2t_imgui INTERFACE)
target_link_libraries(ssf2t_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET ssf2t_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(SSF2T_TRG_VERSION DIRECTORY "${_trg_dir}" DEFINITION PROJECT_VERSION)

set(SSF2T_SOURCES
    src/main.cpp
    src/aspect_render.cpp
    src/frame_counter.cpp
    src/game_files.cpp
    src/graphics_adapters.cpp
    src/launcher.cpp
    src/modal_dialog.cpp
    src/platform.cpp
    src/settings.cpp
    src/stfs.cpp
    src/user_data.cpp
    src/window_host.cpp
)
if(WIN32)
    # Resource 1 is the first icon in the executable: Explorer, and SDL's window class, use it.
    enable_language(RC)
    set(SSF2T_APP_RC "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/ssf2t_hd_remix.rc")
    set_source_files_properties("${SSF2T_APP_RC}" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/app-icon.ico")
    list(APPEND SSF2T_SOURCES "${SSF2T_APP_RC}")
    add_executable(ssf2t_hd_remix WIN32 ${SSF2T_SOURCES})
else()
    add_executable(ssf2t_hd_remix ${SSF2T_SOURCES})
endif()

rexglue_setup_target(ssf2t_hd_remix GPU_PLUGINS xenos)
target_link_libraries(ssf2t_hd_remix PRIVATE trg::launcher)
if(WIN32)
    target_link_libraries(ssf2t_hd_remix PRIVATE shcore synchronization dxgi)
    foreach(_sdk_dll rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
        if(EXISTS "${SSF2T_SDK_DIR}/bin/${_sdk_dll}.dll")
            set_property(TARGET ssf2t_hd_remix APPEND PROPERTY LINK_DEPENDS "${SSF2T_SDK_DIR}/bin/${_sdk_dll}.dll")
        endif()
    endforeach()
else()
    add_custom_command(TARGET ssf2t_hd_remix POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssf2t_hd_remix>"
        VERBATIM
    )
    foreach(_sdk_lib rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
        if(EXISTS "${SSF2T_SDK_DIR}/lib/lib${_sdk_lib}.so")
            set_property(TARGET ssf2t_hd_remix APPEND PROPERTY LINK_DEPENDS "${SSF2T_SDK_DIR}/lib/lib${_sdk_lib}.so")
        endif()
    endforeach()
endif()

# Every build checks that tools/rexglue holds the runtime and GPU plugin built from this branch's patches, and that
# the copies next to the program are those; tools/build.ps1 rebuilds them when they are not.
if(WIN32)
    find_program(SSF2T_WINDOWS_POWERSHELL NAMES powershell REQUIRED)
    set(SSF2T_REXGLUE_CHECK "${CMAKE_COMMAND}" -E env --unset=PSModulePath
        "${SSF2T_WINDOWS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.ps1" -Check -Config $<CONFIG>)
    set(SSF2T_REXGLUE_STAGED -Staged)
endif()
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|Release)$")
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE to Debug, RelWithDebInfo or Release (tools/build.ps1 does)")
endif()
if(WIN32)
    add_custom_target(ssf2t_rexglue_check COMMAND ${SSF2T_REXGLUE_CHECK} VERBATIM)
    add_dependencies(ssf2t_hd_remix ssf2t_rexglue_check)
    add_custom_target(ssf2t_rexglue_staging ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE:rex::gpu-xenos>"
            "$<TARGET_FILE_DIR:ssf2t_hd_remix>"
        COMMAND ${SSF2T_REXGLUE_CHECK} ${SSF2T_REXGLUE_STAGED} "$<TARGET_FILE_DIR:ssf2t_hd_remix>"
        VERBATIM
    )
    add_dependencies(ssf2t_rexglue_staging ssf2t_hd_remix)
endif()

# ThinLTO in Release and RelWithDebInfo; Debug compiles at -O0, where LTO would optimize nothing.
function(ssf2t_enable_thinlto target)
    set(_lto "$<CONFIG:Release,RelWithDebInfo>")
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        set(_link -flto=thin "$<$<CONFIG:Release>:-Wl,/opt:lldlto=3>"
            "-Wl,/lldltocache:${CMAKE_BINARY_DIR}/lto-cache")
    elseif(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_link -flto=thin -fuse-ld=lld "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto-cache")
    else()
        return()
    endif()
    target_compile_options(${target} PRIVATE "$<$<AND:${_lto},$<COMPILE_LANGUAGE:CXX>>:-flto=thin>")
    target_link_options(${target} PRIVATE "$<${_lto}:${_link}>")
endfunction()
ssf2t_enable_thinlto(ssf2t_hd_remix)
target_compile_definitions(ssf2t_hd_remix PRIVATE
    SSF2T_PORT_VERSION="${PROJECT_VERSION}"
    SSF2T_TRG_VERSION="${SSF2T_TRG_VERSION}")

# The runtime reads its cvars from <exe dir>/ssf2t_hd_remix.toml.
add_custom_command(TARGET ssf2t_hd_remix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/ssf2t_hd_remix.toml"
        "$<TARGET_FILE_DIR:ssf2t_hd_remix>"
    VERBATIM
)
