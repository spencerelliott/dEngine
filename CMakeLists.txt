cmake_minimum_required(VERSION 3.0)
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
set(DATA_DIR "${CMAKE_SOURCE_DIR}/data")

# Where the converted runtime asset tree is written. build_disc.sh and
# run_dctool_ip.sh both expect it at disc/.
set(DISC_DIR "${CMAKE_SOURCE_DIR}/disc" CACHE PATH "Destination for the converted runtime asset tree")

# Pixel format textures are converted to. The _pvr.mtl files reference the
# converted files by name, so changing this means updating them to match.
set(DREAMCAST_TEXTURE_FORMAT "ARGB4444" CACHE STRING "Pixel format texconv converts textures to")
option(DREAMCAST_TEXTURE_COMPRESS "VQ compress converted textures" ON)

message("\nPLATFORM_DREAMCAST: ${PLATFORM_DREAMCAST}")

# Dreamcast builds talk to the PowerVR through KallistiOS directly. Flip this on
# to fall back to the old GLdc/OpenGL renderer instead.
option(DREAMCAST_USE_GLDC "Render through GLdc/OpenGL on Dreamcast instead of the native PVR API" OFF)

if(PLATFORM_DREAMCAST AND NOT DREAMCAST_USE_GLDC)
    set(USE_PVR_RENDERER ON)
endif()

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
        src/devtools.h
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
        src/stats.h
        src/texture.h
        src/timer.h
        src/world.h
        src/profiler.h

        src/devtools.c
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
        src/stats.c
        src/texture.c
        src/timer.c
        src/world.c
        src/profiler.c

        src/main.c
)

# Pick the rendering backend
if(USE_PVR_RENDERER)
    list(APPEND SOURCES src/renderer_pvr.h src/renderer_pvr.c)
else()
    list(APPEND SOURCES src/renderer_fixed.h src/renderer_fixed.c)
endif()

# Determine whether we are building Debug or Release
string(TOLOWER ${CMAKE_BUILD_TYPE} CONFIGURATION)

if(NOT USE_PVR_RENDERER)
    include_directories(GLdc/include)
    link_directories(GLdc/lib/${CONFIGURATION})
endif()

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

    if(USE_PVR_RENDERER)
        message("Using the native PowerVR renderer (no OpenGL dependency)")
        add_definitions(-DUSE_PVR_RENDERER)
    else()
        message("Using the GLdc OpenGL renderer")
        link_libraries(GLdc)
    endif()
else()

endif()

execute_process(
    COMMAND git describe --abbrev=4 --dirty --always --tags
    OUTPUT_VARIABLE BUILD_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

link_libraries(m)

if(PLATFORM_DREAMCAST)
    add_executable(dEngine ${EXTRA_OBJS} ${SOURCES})
else()
    add_definitions(-g)
    if(WIN32)
        add_executable(dEngine.exe ${EXTRA_OBJS} ${SOURCES})
    else()
        add_executable(dEngine ${EXTRA_OBJS} ${SOURCES})
    endif()
endif()

# Asset conversion
#
# Builds the runtime tree under ${DISC_DIR} out of data/: PNGs become DTEX (.pvr)
# textures via KallistiOS' texconv, everything else is copied across. Rules are
# per file, so only what actually changed is reconverted.
#
# The file lists are gathered at configure time, so adding or removing an asset
# needs a re-run of cmake to be picked up.
if(PLATFORM_DREAMCAST)
    set(ASSET_OUTPUTS "")

    if(NOT EXISTS "${TEXCONV}")
        message(WARNING
                "texconv was not found at '${TEXCONV}' (is KOS_BASE set?). "
                "Textures will not be converted, so ${DISC_DIR} will be missing "
                "them and the engine will fail to load its materials.")
    else()
        string(TOLOWER "${DREAMCAST_TEXTURE_FORMAT}" TEXTURE_SUFFIX)

        if(DREAMCAST_TEXTURE_COMPRESS)
            set(TEXCONV_COMPRESS_FLAG -c)
        else()
            set(TEXCONV_COMPRESS_FLAG)
        endif()

        file(GLOB_RECURSE ASSET_TEXTURES "${DATA_DIR}/*.png")

        foreach(TEXTURE ${ASSET_TEXTURES})
            file(RELATIVE_PATH TEXTURE_REL "${DATA_DIR}" "${TEXTURE}")

            # Textures live beside the originals in texturesPVR/; anything else
            # (the fonts) keeps its place in the tree.
            if(TEXTURE_REL MATCHES "^textures/")
                string(REGEX REPLACE "^textures/" "texturesPVR/" TEXTURE_OUT_REL "${TEXTURE_REL}")
            else()
                set(TEXTURE_OUT_REL "${TEXTURE_REL}")
            endif()

            set(TEXTURE_OUT "${DISC_DIR}/data/${TEXTURE_OUT_REL}.${TEXTURE_SUFFIX}.pvr")
            get_filename_component(TEXTURE_OUT_DIR "${TEXTURE_OUT}" DIRECTORY)

            add_custom_command(
                    OUTPUT "${TEXTURE_OUT}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${TEXTURE_OUT_DIR}"
                    COMMAND ${TEXCONV} ${TEXCONV_COMPRESS_FLAG} -f ${DREAMCAST_TEXTURE_FORMAT}
                            -i "${TEXTURE}" -o "${TEXTURE_OUT}"
                    DEPENDS "${TEXTURE}"
                    COMMENT "Converting ${TEXTURE_REL} to ${DREAMCAST_TEXTURE_FORMAT}"
                    VERBATIM
            )

            list(APPEND ASSET_OUTPUTS "${TEXTURE_OUT}")
        endforeach()
    endif()

    # Everything the engine loads verbatim. Shaders are left out on purpose:
    # the Dreamcast has no use for them.
    set(ASSET_COPY_EXTENSIONS cp dat map mtl obj md5mesh md5anim scene cfg lbr)
    set(ASSET_SOURCES "")

    foreach(EXTENSION ${ASSET_COPY_EXTENSIONS})
        file(GLOB_RECURSE FOUND_ASSETS "${DATA_DIR}/*.${EXTENSION}")
        list(APPEND ASSET_SOURCES ${FOUND_ASSETS})
    endforeach()

    foreach(ASSET ${ASSET_SOURCES})
        file(RELATIVE_PATH ASSET_REL "${DATA_DIR}" "${ASSET}")

        set(ASSET_OUT "${DISC_DIR}/data/${ASSET_REL}")
        get_filename_component(ASSET_OUT_DIR "${ASSET_OUT}" DIRECTORY)

        add_custom_command(
                OUTPUT "${ASSET_OUT}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSET_OUT_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASSET}" "${ASSET_OUT}"
                DEPENDS "${ASSET}"
                COMMENT "Copying ${ASSET_REL}"
                VERBATIM
        )

        list(APPEND ASSET_OUTPUTS "${ASSET_OUT}")
    endforeach()

    add_custom_target(assets DEPENDS ${ASSET_OUTPUTS})
    add_dependencies(dEngine assets)
endif()
