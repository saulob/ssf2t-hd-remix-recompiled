cmake_minimum_required(VERSION 3.20)
project(trg_launcher VERSION 1.1.0 LANGUAGES C CXX)

# TRG Launcher: the shared pre-game launcher for TekRant Gaming PC ports.
#
#   trg::launcher             the launcher itself (draws into any Dear ImGui frame)
#   trg::launcher_standalone  its own SDL2 + OpenGL 3 window (TRG_LAUNCHER_STANDALONE)
#
# Dear ImGui 1.92+ comes from the parent project when it has a target for it
# (TRG_LAUNCHER_IMGUI_TARGET, or a target named `imgui`), otherwise it is
# downloaded. See docs/INTEGRATION.md.

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(_trg_top_level ON)
else()
  set(_trg_top_level OFF)
endif()

option(TRG_LAUNCHER_STANDALONE "Build the SDL2 + OpenGL 3 standalone window" ${_trg_top_level})
option(TRG_LAUNCHER_BUILD_DEMO "Build the demo launcher" ${_trg_top_level})
option(TRG_LAUNCHER_FETCH_DEPS "Download Dear ImGui / SDL2 when the parent project does not provide them" ON)
set(TRG_LAUNCHER_IMGUI_TARGET "" CACHE STRING "Existing Dear ImGui target to use (empty: `imgui` if it exists, else download)")
set(TRG_LAUNCHER_IMGUI_DIR "" CACHE PATH "Dear ImGui source folder, for the SDL2/OpenGL3 backends of the standalone window")
option(TRG_LAUNCHER_IMGUI_HAS_BACKENDS "The ImGui target already compiles imgui_impl_sdl2 and imgui_impl_opengl3" OFF)
set(TRG_LAUNCHER_IMGUI_VERSION "v1.92.9b" CACHE STRING "Dear ImGui tag to download")
set(TRG_LAUNCHER_SDL2_VERSION "release-2.32.10" CACHE STRING "SDL2 tag to download")

include(FetchContent)

# ------------------------------------------------------------- Dear ImGui ---
if(TRG_LAUNCHER_IMGUI_TARGET)
  set(_trg_imgui ${TRG_LAUNCHER_IMGUI_TARGET})
elseif(TARGET imgui)
  set(_trg_imgui imgui)
elseif(TARGET imgui::imgui)
  set(_trg_imgui imgui::imgui)
elseif(TRG_LAUNCHER_FETCH_DEPS)
  FetchContent_Declare(trg_imgui_src
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG ${TRG_LAUNCHER_IMGUI_VERSION}
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(trg_imgui_src)
  set(TRG_LAUNCHER_IMGUI_DIR ${trg_imgui_src_SOURCE_DIR} CACHE PATH "" FORCE)
  add_library(trg_imgui STATIC
    ${trg_imgui_src_SOURCE_DIR}/imgui.cpp
    ${trg_imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${trg_imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${trg_imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${trg_imgui_src_SOURCE_DIR}/imgui_demo.cpp)
  target_include_directories(trg_imgui PUBLIC ${trg_imgui_src_SOURCE_DIR})
  target_compile_features(trg_imgui PUBLIC cxx_std_17)
  set(_trg_imgui trg_imgui)
else()
  message(FATAL_ERROR "TRG Launcher: no Dear ImGui target. Set TRG_LAUNCHER_IMGUI_TARGET or TRG_LAUNCHER_FETCH_DEPS=ON.")
endif()

# ---------------------------------------------------------------- library ---
add_library(trg_launcher STATIC
  src/launcher.cpp
  src/headers.cpp
  src/settings.cpp
  src/platform.cpp
  src/xbox360.cpp
  src/download.cpp
  src/shader_pack.cpp
  src/game_helpers.cpp)
add_library(trg::launcher ALIAS trg_launcher)
target_include_directories(trg_launcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(trg_launcher PUBLIC cxx_std_17)
target_link_libraries(trg_launcher PUBLIC ${_trg_imgui})
if(WIN32)
  target_link_libraries(trg_launcher PRIVATE shell32 ole32 winhttp dbghelp winmm)
endif()
if(MSVC)
  target_compile_options(trg_launcher PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(trg_launcher PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------ standalone window ---
if(TRG_LAUNCHER_STANDALONE)
  if(NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
    find_package(SDL2 CONFIG QUIET)
  endif()
  if(TARGET SDL2::SDL2-static)
    set(_trg_sdl2 SDL2::SDL2-static)
  elseif(TARGET SDL2::SDL2)
    set(_trg_sdl2 SDL2::SDL2)
  elseif(TRG_LAUNCHER_FETCH_DEPS)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(trg_sdl2_src
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG ${TRG_LAUNCHER_SDL2_VERSION}
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(trg_sdl2_src)
    set(_trg_sdl2 SDL2::SDL2-static)
  else()
    message(FATAL_ERROR "TRG Launcher: TRG_LAUNCHER_STANDALONE needs SDL2 (an SDL2::SDL2 target, or TRG_LAUNCHER_FETCH_DEPS=ON).")
  endif()

  add_library(trg_launcher_standalone STATIC src/standalone_sdl2.cpp)
  add_library(trg::launcher_standalone ALIAS trg_launcher_standalone)
  target_link_libraries(trg_launcher_standalone PUBLIC trg_launcher ${_trg_sdl2})
  if(NOT TRG_LAUNCHER_IMGUI_HAS_BACKENDS)
    if(NOT TRG_LAUNCHER_IMGUI_DIR)
      message(FATAL_ERROR "TRG Launcher: set TRG_LAUNCHER_IMGUI_DIR to the Dear ImGui folder (for its SDL2/OpenGL3 backends), or TRG_LAUNCHER_IMGUI_HAS_BACKENDS=ON.")
    endif()
    if(EXISTS ${TRG_LAUNCHER_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp)
      set(_trg_backends ${TRG_LAUNCHER_IMGUI_DIR}/backends)
    else()
      set(_trg_backends ${TRG_LAUNCHER_IMGUI_DIR})
    endif()
    target_sources(trg_launcher_standalone PRIVATE
      ${_trg_backends}/imgui_impl_sdl2.cpp
      ${_trg_backends}/imgui_impl_opengl3.cpp)
    target_include_directories(trg_launcher_standalone PUBLIC ${_trg_backends})
  endif()
  if(MSVC)
    target_compile_options(trg_launcher_standalone PRIVATE /utf-8)
  endif()
endif()

# ------------------------------------------------------------------ tests ---
if(_trg_top_level)
  enable_testing()
  add_executable(trg_settings_test tests/settings_test.cpp)
  target_link_libraries(trg_settings_test PRIVATE trg_launcher)
  add_test(NAME settings COMMAND trg_settings_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_executable(trg_helpers_test tests/helpers_test.cpp)
  target_link_libraries(trg_helpers_test PRIVATE trg_launcher)
  add_test(NAME helpers COMMAND trg_helpers_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

# ------------------------------------------------------------------- demo ---
if(TRG_LAUNCHER_BUILD_DEMO AND TRG_LAUNCHER_STANDALONE)
  add_subdirectory(examples/demo)
endif()
