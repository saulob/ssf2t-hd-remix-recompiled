# Makes DEST hold exactly the valid packages src/character/character_packages.h names. A missing, different or unknown
# file never stays in the runtime folder, whatever an earlier build left there; the packaging scripts refuse a partial set.
#   cmake -DSOURCE=<folder> -DDEST=<folder> -DTABLE=<character_packages.h> -P stage_character_packages.cmake

cmake_minimum_required(VERSION 3.25)

file(STRINGS "${TABLE}" entries REGEX "^ *{\"[a-z0-9_]+\\.bin\", [0-9]+, \"[0-9a-f]+\"")
if(NOT entries)
    message(FATAL_ERROR "No character packages found in ${TABLE}")
endif()

file(MAKE_DIRECTORY "${DEST}")
set(known "")
set(staged 0)
set(total 0)
foreach(entry IN LISTS entries)
    if(NOT entry MATCHES "\"([a-z0-9_]+\\.bin)\", ([0-9]+), \"([0-9a-f]+)\"")
        message(FATAL_ERROR "Unreadable character package line: ${entry}")
    endif()
    set(name "${CMAKE_MATCH_1}")
    set(size "${CMAKE_MATCH_2}")
    set(sha256 "${CMAKE_MATCH_3}")
    list(APPEND known "${name}")
    math(EXPR total "${total} + 1")

    set(problem "")
    if(NOT EXISTS "${SOURCE}/${name}")
        set(problem "missing")
    else()
        file(SIZE "${SOURCE}/${name}" actual_size)
        file(SHA256 "${SOURCE}/${name}" actual_sha256)
        if(NOT actual_size EQUAL size OR NOT actual_sha256 STREQUAL sha256)
            set(problem "different from src/character/character_packages.h")
        endif()
    endif()

    if(problem)
        message(WARNING "Character package ${name} is ${problem}; it is not staged")
        file(REMOVE "${DEST}/${name}")
    else()
        file(COPY_FILE "${SOURCE}/${name}" "${DEST}/${name}" ONLY_IF_DIFFERENT)
        math(EXPR staged "${staged} + 1")
    endif()
endforeach()

file(GLOB leftovers RELATIVE "${DEST}" "${DEST}/*")
foreach(leftover IN LISTS leftovers)
    if(NOT leftover IN_LIST known)
        file(REMOVE_RECURSE "${DEST}/${leftover}")
    endif()
endforeach()

message(STATUS "Character packages: ${staged} of ${total} staged")
