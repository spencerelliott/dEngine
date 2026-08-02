# 3.12 is the floor for file(GLOB CONFIGURE_DEPENDS), which is what makes the
# game/ directory pick itself up. It also clears CMake 4.x's hard minimum of 3.5.
cmake_minimum_required(VERSION 3.12)
project(dEngine C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(GENROMFS "$ENV{KOS_BASE}/utils/genromfs/genromfs")
set(VQENC $ENV{KOS_BASE}/utils/vqenc/vqenc)
set(TEXCONV $ENV{KOS_BASE}/utils/texconv/texconv)
set(DCBUMPGEN $ENV{KOS_BASE}/utils/dcbumpgen/dcbumpgen)
set(BIN2O $ENV{KOS_BASE}/utils/bin2o/bin2o)
set(ROMDISK_IMG "${CMAKE_SOURCE_DIR}/romdisk/romdisk.img")
set(ROMDISK_O "${CMAKE_SOURCE_DIR}/romdisk/romdisk.o")
set(ROMDISK_DIR "${CMAKE_SOURCE_DIR}/romdisk")
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")

message("\nPLATFORM_DREAMCAST: ${PLATFORM_DREAMCAST}")

set(CMAKE_C_STANDARD 99)

include_directories(src)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math")

IF( EXISTS "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json" )
  EXECUTE_PROCESS( COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
  )
ENDIF()

set(
    SOURCES
        src/sh4_math.h
        src/ItextureLoader.h
        src/camera.h
        src/commands.h
        src/config.h
        src/dEngine.h
        src/entities.h
        src/filesystem.h
        src/font.h
        src/globals.h
        src/lexer.h
        src/material.h
        src/internal_math.c
        src/internal_math.h
        src/matrix.h
        src/md5model.h
        src/obj.h
        src/quaternion.h
        src/renderer.h
        src/renderer_fixed.h
        src/stats.h
        src/texture.h
        src/timer.h
        src/world.h
        src/profiler.h

        src/dgame.h
        src/game_internal.h
        src/input.h

        src/ItextureLoader.c
        src/camera.c
        src/camera.h
        src/commands.c
        src/commands.h
        src/config.c
        src/config.h
        src/dEngine.c
        src/entities.c
        src/filesystem.c
        src/font.c
        src/lexer.c
        src/material.c
        src/matrix.c
        src/md5anim.c
        src/md5model.c
        src/obj.c
        src/quaternion.c
        src/renderer.c
        src/renderer_fixed.c
        src/stats.c
        src/texture.c
        src/timer.c
        src/world.c
        src/profiler.c

        src/game_hooks.c
        src/game_object.c
        src/game_runtime.c
        src/input.c
        src/input_actions.c

        src/main.c
)

# Anything dropped into game/ is compiled in automatically. CONFIGURE_DEPENDS
# makes the Makefile and Ninja generators re-run this glob at build time, so
# adding or removing a script triggers a reconfigure on the next build with no
# manual cmake invocation.
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/game/*.c")
list(SORT GAME_SOURCES)
list(LENGTH GAME_SOURCES GAME_SOURCE_COUNT)
message(STATUS "Game sources found: ${GAME_SOURCE_COUNT}")

# A hook defined without including dgame.h has no visible prototype, so it would
# silently fail to match the engine's signature. Warn on that.
if(GAME_SOURCES)
    set_source_files_properties(${GAME_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wmissing-prototypes;-Wstrict-prototypes")
endif()

# Determine whether we are building Debug or Release.
# An unset CMAKE_BUILD_TYPE used to make the string() call below fail outright,
# so a bare `cmake -S . -B build` could not configure at all.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

string(TOLOWER ${CMAKE_BUILD_TYPE} CONFIGURATION)

include_directories(GLdc/include)
link_directories(GLdc/lib/${CONFIGURATION})

if(ENGINE_DEBUG)
    add_definitions(-DENGINE_DEBUG)
endif()

if(PLATFORM_DREAMCAST)
    add_compile_options(-ffast-math -mfsrra)

    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -fexpensive-optimizations -fomit-frame-pointer -finline-functions")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O3 -fexpensive-optimizations -fomit-frame-pointer -finline-functions")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -Wall -Wextra")

    add_link_options("-Wl,-Map=output.map")

    if(EXISTS ${ROMDISK_DIR})
        if(EXISTS ${ASSETS_DIR})
            message("Setting up romdisk generation...")
            add_custom_command(
                    OUTPUT ${ROMDISK_IMG}
                    COMMAND ${GENROMFS} -f ${ROMDISK_IMG} -d ${ASSETS_DIR} -v
            )

            add_custom_command(
                    OUTPUT ${ROMDISK_O}
                    COMMAND ${BIN2O} romdisk.img romdisk romdisk.o
                    DEPENDS ${ROMDISK_IMG}
                    WORKING_DIRECTORY ${ROMDISK_DIR}
            )

            set(EXTRA_OBJS ${ROMDISK_O})
            include_directories(${ROMDISK_DIR})
        endif()
    endif()

    add_definitions(-DDREAMCAST)

    link_libraries(GLdc)
else()

endif()

execute_process(
    COMMAND git describe --abbrev=4 --dirty --always --tags
    OUTPUT_VARIABLE BUILD_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

link_libraries(m)

# Game sources are passed as objects rather than bundled into a static library.
# An archive member is only pulled into the link if it resolves a symbol that is
# still undefined -- and the weak defaults in game_hooks.c mean every hook is
# already defined by then, so a game's strong override would never be extracted
# and the no-op would silently win.
if(PLATFORM_DREAMCAST)
    add_executable(dEngine ${EXTRA_OBJS} ${SOURCES} ${GAME_SOURCES})
else()
    add_definitions(-g)
    if(WIN32)
        add_executable(dEngine.exe ${EXTRA_OBJS} ${SOURCES} ${GAME_SOURCES})
    else()
        add_executable(dEngine ${EXTRA_OBJS} ${SOURCES} ${GAME_SOURCES})
    endif()
endif()
