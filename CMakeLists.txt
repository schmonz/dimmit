cmake_minimum_required(VERSION 3.15)

# Minimum macOS 12 required for kIOMainPortDefault API
if (APPLE)
    # Default macOS deployment target
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

    # Build Universal binaries (arm64 + x86_64) by default on macOS.
    # Do not override if the user/toolchain has already specified architectures
    # in the CMake cache or via the command line/toolchain file.
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures for macOS (default: Universal)")
    else()
        # If a previous single-arch value lingers in the cache (e.g., from prior manual builds),
        # upgrade it to Universal by default unless the user opts out.
        set(DIMMIT_RESPECT_EXISTING_ARCH OFF CACHE BOOL "Keep existing CMAKE_OSX_ARCHITECTURES cache value if set")
        if (NOT DIMMIT_RESPECT_EXISTING_ARCH)
            if (CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
                set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures for macOS (default: Universal)" FORCE)
            endif()
        endif()
    endif ()
endif ()

project(dimmit LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckSymbolExists)
include(GNUInstallDirs)

set(DIMMIT_SOCK_DEFAULT "/tmp/dimmit.sock" CACHE STRING "Default Unix socket path for dimmit")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service_systemd.service
        ${CMAKE_CURRENT_BINARY_DIR}/service_systemd.service @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service_netbsd.sh
        ${CMAKE_CURRENT_BINARY_DIR}/service_netbsd.sh @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service_darwin.plist
        ${CMAKE_CURRENT_BINARY_DIR}/service_darwin.plist @ONLY)

find_package(Threads REQUIRED)

set(PLATFORM_LIBS)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND PLATFORM_LIBS rt)
    # On Linux, link against libddcutil directly
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(DDCUTIL ddcutil)
    endif ()
    if (DDCUTIL_FOUND)
        list(APPEND PLATFORM_LIBS ${DDCUTIL_LIBRARIES})
    else ()
        message(FATAL_ERROR "ddcutil not found. Please install libddcutil development files.")
    endif ()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS frameworks needed by the built-in implementation
    list(APPEND PLATFORM_LIBS
        "-framework IOKit"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
        "-framework CoreDisplay"
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    # No extra libs required on NetBSD
endif ()

add_executable(dimmitd
    dimmitd.c
    ddc.c
)

string(TOLOWER "${CMAKE_SYSTEM_NAME}" SYSTEM_LOWER)
set(DDC_IMPL_SRC "ddc_impl_${SYSTEM_LOWER}.c")

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${DDC_IMPL_SRC}")
    target_sources(dimmitd PRIVATE "${DDC_IMPL_SRC}")
else ()
    message(FATAL_ERROR "Unsupported platform for DDC implementation: ${CMAKE_SYSTEM_NAME} (Expected ${DDC_IMPL_SRC} to exist)")
endif ()
target_include_directories(dimmitd
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_options(dimmitd
    PRIVATE
)
target_link_libraries(dimmitd
    PRIVATE
    Threads::Threads
    ${PLATFORM_LIBS}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(dimmitd PRIVATE ${DDCUTIL_INCLUDE_DIRS})
    target_compile_options(dimmitd PRIVATE ${DDCUTIL_CFLAGS_OTHER})
endif ()

add_executable(dimmit-up
    dimmit.c
)
add_executable(dimmit-down
    dimmit.c
)
target_include_directories(dimmit-up
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(dimmit-down
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

install(TARGETS dimmitd
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
)

install(TARGETS dimmit-up dimmit-down
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
