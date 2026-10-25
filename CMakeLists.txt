# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ssf2t_hd_remix VERSION 0.2.1 LANGUAGES C CXX)

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

# The TRG Launcher in third_party stays as upstream published it. Every build compiles a patched copy under the
# binary directory; the copy is rebuilt only when the source or a patch changes.
set(SSF2T_TRG_LAUNCHER_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/patches/trg-launcher")
set(SSF2T_TRG_LAUNCHER_PATCH_STAMP_VERSION 1)
file(GLOB _trg_patch_names CONFIGURE_DEPENDS RELATIVE "${SSF2T_TRG_LAUNCHER_PATCH_DIR}"
    "${SSF2T_TRG_LAUNCHER_PATCH_DIR}/*.patch")
list(SORT _trg_patch_names)
foreach(_trg_patch_name IN LISTS _trg_patch_names)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${SSF2T_TRG_LAUNCHER_PATCH_DIR}/${_trg_patch_name}")
endforeach()

find_package(Git QUIET)
if(NOT Git_FOUND)
    message(FATAL_ERROR "TRG Launcher: git is required to apply patches/trg-launcher")
endif()

# GIT_CEILING_DIRECTORIES keeps git apply from discovering this repository around the generated copy.
function(ssf2t_prepare_patched_trg_launcher source_dir output_dir)
    file(GLOB_RECURSE _source_files LIST_DIRECTORIES false RELATIVE "${source_dir}" "${source_dir}/*")
    list(SORT _source_files)
    set(_source_state "")
    foreach(_file IN LISTS _source_files)
        file(SHA256 "${source_dir}/${_file}" _file_hash)
        string(APPEND _source_state "${_file}\n${_file_hash}\n")
    endforeach()
    set(_fingerprint "${SSF2T_TRG_LAUNCHER_PATCH_STAMP_VERSION}\n${source_dir}\n${_source_state}")
    foreach(_patch_name IN LISTS _trg_patch_names)
        file(SHA256 "${SSF2T_TRG_LAUNCHER_PATCH_DIR}/${_patch_name}" _patch_hash)
        string(APPEND _fingerprint "${_patch_name}\n${_patch_hash}\n")
    endforeach()
    string(SHA256 _fingerprint "${_fingerprint}")

    set(_stamp "${output_dir}/.ssf2t-trg-patch-stamp")
    if(EXISTS "${_stamp}" AND EXISTS "${output_dir}/CMakeLists.txt")
        file(READ "${_stamp}" _stamp_value)
        if(_stamp_value STREQUAL _fingerprint)
            return()
        endif()
    endif()

    message(STATUS "TRG Launcher: preparing patched copy in ${output_dir}")
    file(REMOVE_RECURSE "${output_dir}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(COPY "${source_dir}/" DESTINATION "${output_dir}")

    get_filename_component(_ceiling "${output_dir}" DIRECTORY)
    foreach(_patch_name IN LISTS _trg_patch_names)
        set(_patch "${SSF2T_TRG_LAUNCHER_PATCH_DIR}/${_patch_name}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                        "${GIT_EXECUTABLE}" -C "${output_dir}" apply --check "${_patch}"
                        RESULT_VARIABLE _can_apply ERROR_VARIABLE _apply_error)
        if(NOT _can_apply EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: ${_patch} does not apply to ${source_dir} (${_apply_error}); the patch and the source are out of sync.")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                        "${GIT_EXECUTABLE}" -C "${output_dir}" apply "${_patch}"
                        RESULT_VARIABLE _apply_result)
        if(NOT _apply_result EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: failed to apply ${_patch}")
        endif()
    endforeach()
    file(WRITE "${_stamp}" "${_fingerprint}")
endfunction()

set(_trg_patched_dir "${CMAKE_BINARY_DIR}/trg-launcher-patched")
ssf2t_prepare_patched_trg_launcher("${_trg_dir}" "${_trg_patched_dir}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_trg_dir}/CMakeLists.txt")

file(READ "${_trg_patched_dir}/src/launcher.cpp" _trg_launcher_source)
string(FIND "${_trg_launcher_source}" "void Launcher::HandleHotkeys() {" _hotkeys_pos)
string(SUBSTRING "${_trg_launcher_source}" ${_hotkeys_pos} -1 _hotkeys_source)
string(FIND "${_hotkeys_source}" "ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)" _ctrl_s_pos)
string(FIND "${_hotkeys_source}" "io.WantTextInput) return;" _text_guard_pos)
if(_hotkeys_pos EQUAL -1 OR _ctrl_s_pos EQUAL -1 OR _text_guard_pos EQUAL -1 OR _ctrl_s_pos GREATER _text_guard_pos)
    message(FATAL_ERROR "TRG Launcher: the staged HandleHotkeys does not handle Ctrl+S before the text-input guard (${_trg_patched_dir}/src/launcher.cpp)")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_trg_patched_dir}/src/launcher.cpp")

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use the ImGui compiled into rexruntime.
add_library(ssf2t_imgui INTERFACE)
target_link_libraries(ssf2t_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET ssf2t_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_patched_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(SSF2T_TRG_VERSION DIRECTORY "${_trg_patched_dir}" DEFINITION PROJECT_VERSION)

set(SSF2T_SOURCES
    src/main.cpp
    src/achievement_ui_hook.cpp
    src/achievements_overlay.cpp
    src/active_log.cpp
    src/aspect_render.cpp
    src/crash_report.cpp
    src/diagnostics.cpp
    src/fight_area_hook.cpp
    src/frame_counter.cpp
    src/game_files.cpp
    src/graphics_adapters.cpp
    src/imgui_gamepad.cpp
    src/guest_functions.cpp
    src/infinite_time.cpp
    src/infinite_time_ui.cpp
    src/input_summary.cpp
    src/menu_text_hook.cpp
    src/launcher.cpp
    src/launcher_achievements.cpp
    src/log_filter.cpp
    src/log_path.cpp
    src/modal_dialog.cpp
    src/platform.cpp
    src/session_log.cpp
    src/settings.cpp
    src/stfs.cpp
    src/system_info.cpp
    src/update_config.cpp
    src/updater/github_release.cpp
    src/updater/json.cpp
    src/updater/sha256.cpp
    src/updater/update_controller.cpp
    src/updater/update_job.cpp
    src/updater/update_paths.cpp
    src/updater/update_platform.cpp
    src/updater/update_ui.cpp
    src/updater/update_zip.cpp
    src/updater/version.cpp
    src/updater/third_party/miniz/miniz.c
    src/user_data.cpp
    src/window_host.cpp
)
set_source_files_properties(src/updater/third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-w")
set_property(SOURCE src/update_config.cpp src/updater/github_release.cpp src/updater/json.cpp src/updater/sha256.cpp
    src/updater/update_controller.cpp src/updater/update_job.cpp src/updater/update_paths.cpp
    src/updater/update_platform.cpp src/updater/update_ui.cpp src/updater/update_zip.cpp src/updater/version.cpp
    APPEND PROPERTY COMPILE_OPTIONS -Wall -Wextra)
if(WIN32)
    # Resource 1 is the first icon in the executable: Explorer, and SDL's window class, use it.
    enable_language(RC)
    set(SSF2T_APP_RC "${CMAKE_CURRENT_BINARY_DIR}/ssf2t_hd_remix.rc")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/ssf2t_hd_remix.rc.in" "${SSF2T_APP_RC}" @ONLY)
    set_source_files_properties("${SSF2T_APP_RC}" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/app-icon.ico")
    list(APPEND SSF2T_SOURCES "${SSF2T_APP_RC}")
    list(APPEND SSF2T_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/ssf2t_hd_remix.manifest")
    add_executable(ssf2t_hd_remix WIN32 ${SSF2T_SOURCES})
else()
    add_executable(ssf2t_hd_remix ${SSF2T_SOURCES})
endif()

rexglue_setup_target(ssf2t_hd_remix GPU_PLUGINS xenos)
target_include_directories(ssf2t_hd_remix PRIVATE src)
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
else()
    find_program(SSF2T_BASH bash REQUIRED)
    set(SSF2T_REXGLUE_CHECK "${SSF2T_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.sh"
        --check "${SSF2T_SDK_DIR}" --config $<CONFIG>)
    set(SSF2T_REXGLUE_STAGED --staged)
endif()
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|Release)$")
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE to Debug, RelWithDebInfo or Release (tools/build.ps1 does)")
endif()
add_custom_target(ssf2t_rexglue_check COMMAND ${SSF2T_REXGLUE_CHECK} VERBATIM)
add_dependencies(ssf2t_hd_remix ssf2t_rexglue_check)
add_custom_target(ssf2t_rexglue_staging ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE:rex::gpu-xenos>"
        "$<TARGET_FILE_DIR:ssf2t_hd_remix>"
    COMMAND ${SSF2T_REXGLUE_CHECK} ${SSF2T_REXGLUE_STAGED} "$<TARGET_FILE_DIR:ssf2t_hd_remix>"
    VERBATIM
)
add_dependencies(ssf2t_rexglue_staging ssf2t_hd_remix)

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

# The update helper runs from the temporary update workspace, away from the installation, so it keeps its runtime
# inside the executable. Release packages ship the MSYS2 CLANG64 build of these sources (tools/build-updater.ps1).
if(WIN32)
    add_executable(trg-updater WIN32
        src/updater/helper_main.cpp
        src/updater/install_transaction.cpp
        src/updater/sha256.cpp
        src/updater/update_job.cpp
        src/updater/update_paths.cpp
        src/updater/update_platform.cpp
    )
    target_include_directories(trg-updater PRIVATE src)
    target_link_libraries(trg-updater PRIVATE shell32)
    target_link_options(trg-updater PRIVATE -static)
    set_property(TARGET trg-updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(trg-updater PRIVATE -Wall -Wextra)
    add_dependencies(ssf2t_hd_remix trg-updater)
endif()
target_compile_definitions(ssf2t_hd_remix PRIVATE
    SSF2T_PORT_VERSION="${PROJECT_VERSION}"
    SSF2T_TRG_VERSION="${SSF2T_TRG_VERSION}"
    SSF2T_BUILD_CONFIG="$<CONFIG>")

# Headless regression tests of the patched runtime, not part of the default build (tools/test.ps1):
#   cmake --build <dir> --target ssf2t_runtime_tests
add_custom_target(ssf2t_runtime_tests)
foreach(_test physical_heap_stress title_thread_shutdown_tests)
    set(_name ssf2t_${_test})
    add_executable(${_name} EXCLUDE_FROM_ALL tests/${_test}.cpp)
    target_link_libraries(${_name} PRIVATE rex::runtime)
    add_custom_command(TARGET ${_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:${_name}>"
        VERBATIM
    )
    if(NOT WIN32)
        set_target_properties(${_name} PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
    add_dependencies(ssf2t_runtime_tests ${_name})
endforeach()

add_executable(ssf2t_window_placement_tests EXCLUDE_FROM_ALL tests/window_placement_tests.cpp)
target_include_directories(ssf2t_window_placement_tests PRIVATE src)
add_dependencies(ssf2t_runtime_tests ssf2t_window_placement_tests)

# Launcher and window behaviour that needs no window: keyboard remapping, Escape not quitting, per-monitor DPI
# helpers, the input-aware cursor and the window title.
foreach(_test keyboard launcher_quit dpi window_host player_name)
    set(_name ssf2t_${_test}_tests)
    add_executable(${_name} EXCLUDE_FROM_ALL tests/${_test}_tests.cpp)
    target_include_directories(${_name} PRIVATE src)
    target_include_directories(${_name} SYSTEM PRIVATE "${SSF2T_SDK_DIR}/include" "${_trg_dir}/include")
    target_compile_definitions(${_name} PRIVATE SSF2T_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_dependencies(ssf2t_runtime_tests ${_name})
endforeach()

add_executable(ssf2t_menu_text_tests EXCLUDE_FROM_ALL tests/menu_text_tests.cpp)
target_include_directories(ssf2t_menu_text_tests PRIVATE src)
target_compile_definitions(ssf2t_menu_text_tests PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(ssf2t_runtime_tests ssf2t_menu_text_tests)

add_executable(ssf2t_launcher_settings_tests EXCLUDE_FROM_ALL tests/launcher_settings_tests.cpp)
target_include_directories(ssf2t_launcher_settings_tests PRIVATE src)
add_dependencies(ssf2t_runtime_tests ssf2t_launcher_settings_tests)

add_executable(ssf2t_fight_area_tests EXCLUDE_FROM_ALL tests/fight_area_tests.cpp)
target_include_directories(ssf2t_fight_area_tests PRIVATE src)
target_compile_definitions(ssf2t_fight_area_tests PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(ssf2t_runtime_tests ssf2t_fight_area_tests)

# Graphics card setting tests, not part of the default build: cmake --build <dir> --target ssf2t_graphics_adapter_tests
add_executable(ssf2t_graphics_adapter_tests EXCLUDE_FROM_ALL tests/graphics_adapter_tests.cpp src/graphics_adapters.cpp)
target_include_directories(ssf2t_graphics_adapter_tests PRIVATE src)
target_include_directories(ssf2t_graphics_adapter_tests SYSTEM PRIVATE "${SSF2T_SDK_DIR}/include" "${_trg_dir}/include")
target_link_libraries(ssf2t_graphics_adapter_tests PRIVATE trg::launcher)
if(WIN32)
    target_link_libraries(ssf2t_graphics_adapter_tests PRIVATE dxgi d3d12)
endif()
target_compile_definitions(ssf2t_graphics_adapter_tests PRIVATE SSF2T_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Infinite Time: the setting, the F10 toggle, the notice and the timer hook decision. Headless.
add_executable(ssf2t_infinite_time_tests EXCLUDE_FROM_ALL tests/infinite_time_tests.cpp
    src/updater/sha256.cpp src/updater/update_paths.cpp)
target_include_directories(ssf2t_infinite_time_tests PRIVATE src)
target_link_libraries(ssf2t_infinite_time_tests PRIVATE trg::launcher)
target_compile_definitions(ssf2t_infinite_time_tests PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(ssf2t_runtime_tests ssf2t_infinite_time_tests)

add_executable(ssf2t_achievement_tests EXCLUDE_FROM_ALL tests/achievement_tests.cpp)
target_include_directories(ssf2t_achievement_tests PRIVATE src "${SSF2T_SDK_DIR}/include")
target_compile_definitions(ssf2t_achievement_tests PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(ssf2t_runtime_tests ssf2t_achievement_tests)

add_executable(ssf2t_game_files_tests EXCLUDE_FROM_ALL tests/game_files_tests.cpp src/game_files.cpp src/stfs.cpp)
target_include_directories(ssf2t_game_files_tests PRIVATE src)
target_compile_definitions(ssf2t_game_files_tests PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(ssf2t_runtime_tests ssf2t_game_files_tests)

# Reads the game's achievements through a runtime without graphics; skips without game\extracted.
add_executable(ssf2t_achievement_metadata_tests EXCLUDE_FROM_ALL tests/achievement_metadata_tests.cpp src/launcher_achievements.cpp)
target_include_directories(ssf2t_achievement_metadata_tests PRIVATE src)
target_compile_definitions(ssf2t_achievement_metadata_tests PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ssf2t_achievement_metadata_tests PRIVATE rex::runtime)
add_custom_command(TARGET ssf2t_achievement_metadata_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssf2t_achievement_metadata_tests>"
    VERBATIM
)
if(NOT WIN32)
    set_target_properties(ssf2t_achievement_metadata_tests PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
add_dependencies(ssf2t_runtime_tests ssf2t_achievement_metadata_tests)

# Runtime patch tests: the patched logic modeled in isolation, and the patch text checked for the changes it must make.
find_package(Threads REQUIRED)
foreach(_test guest_clock d3d12_transfer_short_circuit d3d12_sampler_memo shared_memory_fast_path
        gpu_draw_state_memo d3d12_async_submission gpu_register_wake)
    set(_name ssf2t_${_test}_tests)
    add_executable(${_name} EXCLUDE_FROM_ALL tests/${_test}_tests.cpp)
    target_include_directories(${_name} PRIVATE src)
    target_compile_definitions(${_name} PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${_name} PRIVATE Threads::Threads)
    ssf2t_enable_thinlto(${_name})
    add_dependencies(ssf2t_runtime_tests ${_name})
endforeach()

# Updater logic without the runtime: versions, release JSON, SHA-256, ZIP safety, staging, jobs and rollback.
add_executable(ssf2t_update_tests EXCLUDE_FROM_ALL
    tests/update_tests.cpp
    src/updater/github_release.cpp
    src/updater/install_transaction.cpp
    src/updater/json.cpp
    src/updater/sha256.cpp
    src/updater/update_job.cpp
    src/updater/update_paths.cpp
    src/updater/update_platform.cpp
    src/updater/update_zip.cpp
    src/updater/version.cpp
    src/updater/third_party/miniz/miniz.c
)
target_include_directories(ssf2t_update_tests PRIVATE src)
target_compile_definitions(ssf2t_update_tests PRIVATE SSF2T_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ssf2t_update_tests PRIVATE -Wall -Wextra)
add_dependencies(ssf2t_runtime_tests ssf2t_update_tests)
if(WIN32)
    target_link_libraries(ssf2t_update_tests PRIVATE shell32)

    add_executable(ssf2t_update_restart_marker WIN32 EXCLUDE_FROM_ALL tests/update_restart_marker.cpp)
    target_link_libraries(ssf2t_update_restart_marker PRIVATE shell32)
    target_link_options(ssf2t_update_restart_marker PRIVATE -static)
    target_compile_options(ssf2t_update_restart_marker PRIVATE -Wall -Wextra)

    add_executable(ssf2t_update_helper_tests EXCLUDE_FROM_ALL tests/update_helper_tests.cpp)
    target_compile_options(ssf2t_update_helper_tests PRIVATE -Wall -Wextra)
    target_compile_definitions(ssf2t_update_helper_tests PRIVATE
        SSF2T_UPDATER_EXE="$<TARGET_FILE:trg-updater>"
        SSF2T_RESTART_MARKER_EXE="$<TARGET_FILE:ssf2t_update_restart_marker>")
    add_dependencies(ssf2t_update_helper_tests trg-updater ssf2t_update_restart_marker)
    add_dependencies(ssf2t_runtime_tests ssf2t_update_helper_tests)
endif()

# The runtime reads its cvars from <exe dir>/ssf2t_hd_remix.toml.
add_custom_command(TARGET ssf2t_hd_remix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/ssf2t_hd_remix.toml"
        "$<TARGET_FILE_DIR:ssf2t_hd_remix>"
    VERBATIM
)

add_custom_command(TARGET ssf2t_hd_remix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ssf2t_hd_remix>/assets/launcher"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/header-art.png"
        "$<TARGET_FILE_DIR:ssf2t_hd_remix>/assets/launcher/header-art.png"
    VERBATIM
)

# Logging, crash report and diagnostics tests, not part of the default build (tools/test.ps1):
#   cmake --build <dir> --target ssf2t_runtime_tests
add_executable(ssf2t_relaunch_tests EXCLUDE_FROM_ALL tests/relaunch_tests.cpp)
target_include_directories(ssf2t_relaunch_tests PRIVATE src)
target_compile_definitions(ssf2t_relaunch_tests PRIVATE SSF2T_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
add_dependencies(ssf2t_runtime_tests ssf2t_relaunch_tests)

add_executable(ssf2t_file_dialog_tests EXCLUDE_FROM_ALL tests/file_dialog_tests.cpp "${_trg_patched_dir}/src/file_dialog.cpp")
target_include_directories(ssf2t_file_dialog_tests PRIVATE "${_trg_patched_dir}/include")
add_dependencies(ssf2t_runtime_tests ssf2t_file_dialog_tests)

add_executable(ssf2t_log_path_tests EXCLUDE_FROM_ALL tests/log_path_tests.cpp src/log_path.cpp)
target_include_directories(ssf2t_log_path_tests PRIVATE src)
add_dependencies(ssf2t_runtime_tests ssf2t_log_path_tests)

add_executable(ssf2t_lifecycle_tests EXCLUDE_FROM_ALL tests/lifecycle_tests.cpp)
target_include_directories(ssf2t_lifecycle_tests PRIVATE src)
target_compile_definitions(ssf2t_lifecycle_tests PRIVATE SSF2T_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
add_dependencies(ssf2t_runtime_tests ssf2t_lifecycle_tests)

add_executable(ssf2t_diagnostics_tests EXCLUDE_FROM_ALL tests/diagnostics_tests.cpp src/diagnostics.cpp)
target_include_directories(ssf2t_diagnostics_tests PRIVATE src)
add_dependencies(ssf2t_runtime_tests ssf2t_diagnostics_tests)

add_executable(ssf2t_crash_report_tests EXCLUDE_FROM_ALL tests/crash_report_tests.cpp src/crash_report.cpp)
target_include_directories(ssf2t_crash_report_tests PRIVATE src)
target_compile_definitions(ssf2t_crash_report_tests PRIVATE
    SSF2T_PORT_VERSION="${PROJECT_VERSION}"
    SSF2T_BUILD_CONFIG="RelWithDebInfo")
if(WIN32)
    target_link_libraries(ssf2t_crash_report_tests PRIVATE shell32)
endif()
add_dependencies(ssf2t_runtime_tests ssf2t_crash_report_tests)

add_executable(ssf2t_active_log_tests EXCLUDE_FROM_ALL tests/active_log_tests.cpp src/active_log.cpp src/log_filter.cpp)
target_include_directories(ssf2t_active_log_tests PRIVATE src)
target_include_directories(ssf2t_active_log_tests SYSTEM PRIVATE "${SSF2T_SDK_DIR}/include")
target_link_libraries(ssf2t_active_log_tests PRIVATE rex::runtime)
add_custom_command(TARGET ssf2t_active_log_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssf2t_active_log_tests>"
    VERBATIM
)
if(NOT WIN32)
    set_target_properties(ssf2t_active_log_tests PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
add_dependencies(ssf2t_runtime_tests ssf2t_active_log_tests)

# Detailed logging, the Minimal log filter and the session log handoff.
add_executable(ssf2t_log_filter_tests EXCLUDE_FROM_ALL
    tests/log_filter_tests.cpp
    src/active_log.cpp
    src/crash_report.cpp
    src/log_filter.cpp
    src/session_log.cpp
)
target_include_directories(ssf2t_log_filter_tests PRIVATE src)
target_include_directories(ssf2t_log_filter_tests SYSTEM PRIVATE "${SSF2T_SDK_DIR}/include")
target_compile_definitions(ssf2t_log_filter_tests PRIVATE
    SSF2T_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
    SSF2T_PORT_VERSION="${PROJECT_VERSION}"
    SSF2T_BUILD_CONFIG="RelWithDebInfo")
target_link_libraries(ssf2t_log_filter_tests PRIVATE rex::runtime)
if(WIN32)
    target_link_libraries(ssf2t_log_filter_tests PRIVATE shell32)
endif()
add_custom_command(TARGET ssf2t_log_filter_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssf2t_log_filter_tests>"
    VERBATIM
)
if(NOT WIN32)
    set_target_properties(ssf2t_log_filter_tests PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
add_dependencies(ssf2t_runtime_tests ssf2t_log_filter_tests)
