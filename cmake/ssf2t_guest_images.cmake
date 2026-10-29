# The recompiled game images that share the executable: default.xex alone (0.0.1.3), always, and with its Title Update
# (0.0.2.3). tools/codegen.ps1 writes generated/<image> and its stamp; nothing here runs codegen, so generated files
# never change while Ninja is running and a precompiled header cannot go stale under it.
# SSF2T_TITLE_UPDATE_IMAGE: AUTO builds the Title Update image when generated/tu exists, ON requires it (releases),
# OFF leaves it out.

set(REXSDK_VERSION "" CACHE STRING "Override SDK version (leave empty for default)")
if(REXSDK_VERSION)
    find_package(rexglue ${REXSDK_VERSION} EXACT QUIET CONFIG)
else()
    find_package(rexglue 0.10.0 QUIET CONFIG)
endif()
if(NOT rexglue_FOUND)
    message(FATAL_ERROR "ReXGlue SDK not found: run tools/codegen.ps1 to unpack it into tools/rexglue")
endif()
message(STATUS "Found ReXGlue SDK ${REXGLUE_VERSION_STRING} at ${rexglue_DIR}")

set(SSF2T_TITLE_UPDATE_IMAGE "AUTO" CACHE STRING "Build the Title Update (0.0.2.3) image: AUTO, ON or OFF")
set_property(CACHE SSF2T_TITLE_UPDATE_IMAGE PROPERTY STRINGS AUTO ON OFF)
set(REXGLUE_RECOMP_DEBUG_INFO "line-tables-only" CACHE STRING
    "Debug info level for generated code: line-tables-only, full, or none")

# Guest SEH scopes emit __try/__except, which traps hardware faults only under asynchronous EH.
set(SSF2T_RECOMP_OPTIONS "")
if(WIN32)
    if(MSVC)
        list(APPEND SSF2T_RECOMP_OPTIONS /EHa)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND SSF2T_RECOMP_OPTIONS -fasync-exceptions)
    endif()
endif()
if(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "none")
    list(APPEND SSF2T_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-g0>)
elseif(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "line-tables-only")
    list(APPEND SSF2T_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)
endif()

# generated/<image> as an object library. Its stamp is a configure dependency and an input of every object, so a
# regenerated image reconfigures the build and recompiles; the precompiled header comes from the same directory.
function(ssf2t_add_guest_image name image)
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated/${image}")
    set(_stamp "${_dir}/ssf2t-guest.stamp")
    if(NOT EXISTS "${_dir}/sources.cmake" OR NOT EXISTS "${_stamp}")
        message(FATAL_ERROR "generated/${image} is missing: run tools/codegen.ps1 (it needs game/extracted/default.xex"
                            " and, for the Title Update image, default.xexp)")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_stamp}")

    include("${_dir}/sources.cmake")
    add_library(${name} OBJECT ${GENERATED_SOURCES})
    set_source_files_properties(${GENERATED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${SSF2T_RECOMP_OPTIONS}"
        OBJECT_DEPENDS "${_stamp}")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src" "${_dir}")
    target_link_libraries(${name} PRIVATE rex::runtime)
    rexglue_apply_target_settings(${name})
    # The options go on the target as well: CMake compiles the header from its own cmake_pch.hxx.cxx, which carries
    # none of the source properties, and clang rejects a PCH whose EH mode disagrees with the TU including it.
    target_precompile_headers(${name} PRIVATE "${_dir}/ssf2t_hd_remix_pch.h")
    target_compile_options(${name} PRIVATE ${SSF2T_RECOMP_OPTIONS})
endfunction()

# Fails the build, before anything compiles, when a generated image no longer matches its sources.
function(ssf2t_check_guest_images host)
    if(WIN32)
        find_program(SSF2T_WINDOWS_POWERSHELL NAMES powershell REQUIRED)
        set(_check "${CMAKE_COMMAND}" -E env --unset=PSModulePath "${SSF2T_WINDOWS_POWERSHELL}" -NoProfile
            -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/codegen.ps1" -Check)
    else()
        find_program(SSF2T_BASH bash REQUIRED)
        set(_check "${SSF2T_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-guest-code.sh" "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    add_custom_target(ssf2t_guest_check COMMAND ${_check} VERBATIM)
    add_dependencies(${host} ssf2t_guest_check)
    foreach(_image IN LISTS SSF2T_GUEST_IMAGES)
        add_dependencies(${_image} ssf2t_guest_check)
    endforeach()
    target_compile_definitions(${host} PRIVATE SSF2T_TITLE_UPDATE_IMAGE=${SSF2T_TITLE_UPDATE_IMAGE_BUILT})
endfunction()

set(SSF2T_GUEST_IMAGES ssf2t_guest_base013)
ssf2t_add_guest_image(ssf2t_guest_base013 base)
if(SSF2T_TITLE_UPDATE_IMAGE STREQUAL "ON" OR
   (SSF2T_TITLE_UPDATE_IMAGE STREQUAL "AUTO" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/tu/ssf2t-guest.stamp"))
    ssf2t_add_guest_image(ssf2t_guest_tu023 tu)
    list(APPEND SSF2T_GUEST_IMAGES ssf2t_guest_tu023)
    set(SSF2T_TITLE_UPDATE_IMAGE_BUILT 1)
else()
    set(SSF2T_TITLE_UPDATE_IMAGE_BUILT 0)
endif()
message(STATUS "Game images: ${SSF2T_GUEST_IMAGES}")
