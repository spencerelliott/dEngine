cmake_minimum_required(VERSION 3.0)
project(dEngine C)

# Enable ExternalProject CMake module
include(ExternalProject)

# Set default ExternalProject root directory
set_directory_properties(PROPERTIES EP_PREFIX ${CMAKE_BINARY_DIR}/Rust)

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
        src/internal_math.c
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

        src/main.c
)

include_directories(GLdc/include)
link_directories(GLdc/lib)

if(ENGINE_DEBUG)
    add_definitions(-DENGINE_DEBUG)
endif()

if(PLATFORM_DREAMCAST)
    #set(SOURCES
    #    ${SOURCES}
    #    src/dc/os_dc.c
    #    src/dc/txr_dc.c
    #    src/dc/gfx_dc.c
    #    src/dc/inp_dc.c
    #    src/dc/snd_dc.c
    #)

    add_compile_options(-g)
    add_link_options("-Wl,-Map=output.map")

    if(EXISTS ${ROMDISK_DIR})
#        message("Generating assets so something is available for the romdisk...")
#        execute_process(
#                COMMAND bash -c "./make_assets.sh dc"
#                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#        )

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

    link_directories(rust/game-code/target/debug/)

    add_definitions(-DDREAMCAST)

    link_libraries(kmg)
    link_libraries(kosutils)
    link_libraries(GLdc)
else()

endif()

execute_process(
    COMMAND git describe --abbrev=4 --dirty --always --tags
    OUTPUT_VARIABLE BUILD_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

#configure_file(GL/version.c.in ${CMAKE_CURRENT_BINARY_DIR}/version.c)
link_libraries(m)

if(PLATFORM_DREAMCAST)
    add_executable(dEngine ${EXTRA_OBJS} ${SOURCES})
else()
    set(SOURCES
            ${SOURCES}
            src/pc/os_pc.c
            src/pc/txr_pc.c
    )

    link_directories(rust/game-code/target/debug/)
    link_libraries(game_code)

    add_definitions(-g)
    if(WIN32)
        add_executable(dEngine.exe ${EXTRA_OBJS} ${SOURCES})
    else()
        add_executable(tile-engine ${EXTRA_OBJS} ${SOURCES})

        # Add rust_code as a CMake target
        ExternalProject_Add(
                rust_code
                DOWNLOAD_COMMAND ""
                CONFIGURE_COMMAND ""
                BUILD_COMMAND cargo build COMMAND cargo build --release
                BINARY_DIR "${CMAKE_SOURCE_DIR}/rust/game-code"
                INSTALL_COMMAND ""
                LOG_BUILD ON
                BUILD_ALWAYS TRUE)

        add_dependencies(tile-engine rust_code)
    endif()
endif()
