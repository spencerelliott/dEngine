set(CMAKE_SYSTEM_NAME Generic)

set(PLATFORM_DREAMCAST TRUE CACHE INTERNAL "" FORCE)

set(CMAKE_SYSTEM_VERSION 1)

set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(WIN32)
    set(CMAKE_C_COMPILER $ENV{KOS_CC_BASE}/bin/sh-elf-gcc.exe CACHE INTERNAL "" FORCE)
else()
    set(CMAKE_C_COMPILER $ENV{KOS_CC_BASE}/bin/sh-elf-gcc CACHE INTERNAL "" FORCE)
endif()

add_compile_options(-ml -m4-single-only -ffunction-sections -fdata-sections)

if($ENV{KOS_SUBARCH} MATCHES naomi)
    set(CMAKE_EXE_LINKER_FLAGS " -ml -m4-single-only -Wl,-Ttext=0x8c020000 -Wl,--gc-sections -T$ENV{KOS_BASE}/utils/ldscripts/shlelf-naomi.xc -nodefaultlibs" CACHE INTERNAL "" FORCE)
else()
    set(CMAKE_EXE_LINKER_FLAGS " -ml -m4-single-only -Wl,-Ttext=0x8c010000 -Wl,--gc-sections -T$ENV{KOS_BASE}/utils/ldscripts/shlelf.xc -nodefaultlibs" CACHE INTERNAL "" FORCE)
endif()

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

set(CMAKE_SYSTEM_INCLUDE_PATH "${CMAKE_SYSTEM_INCLUDE_PATH} $ENV{KOS_BASE}/include $ENV{KOS_BASE}/kernel/arch/dreamcast/include $ENV{KOS_BASE}/addons/include $ENV{KOS_BASE}/../kos-ports/include")

include_directories(
    $ENV{KOS_BASE}/include
    $ENV{KOS_BASE}/kernel/arch/dreamcast/include
    $ENV{KOS_BASE}/addons/include
    $ENV{KOS_BASE}/../kos-ports/include
)

link_directories(
    $ENV{KOS_BASE}/addons/lib/dreamcast
    $ENV{KOS_PORTS}/lib
)

if(${CMAKE_BUILD_TYPE} MATCHES Debug)
    link_directories($ENV{KOS_BASE}/lib/dreamcast/debug)
else()
    link_directories($ENV{KOS_BASE}/lib/dreamcast)
endif()


add_link_options(-L$ENV{KOS_BASE}/lib/dreamcast)
link_libraries(-Wl,--start-group -lkallisti -lc -lgcc -Wl,--end-group m)

set(CMAKE_EXECUTABLE_SUFFIX_C ".elf" CACHE INTERNAL "" FORCE)

add_definitions(
    -D__DREAMCAST__
    -DDREAMCAST
    -D_arch_dreamcast
    -D__arch_dreamcast
    -D_arch_sub_$ENV{KOS_SUBARCH}
)

if (NOT CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DNDEBUG)
endif()

set(CMAKE_ASM_FLAGS "")
set(CMAKE_ASM_FLAGS_RELEASE "")