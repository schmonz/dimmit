cmake_minimum_required(VERSION 3.15)

# Minimum macOS 12 required for kIOMainPortDefault API
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
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
    add_library(ddcutil_macos STATIC
        ddcutil_macos.c
        ddcutil_compat.h
    )
    target_link_libraries(ddcutil_macos
        PUBLIC
        "-framework IOKit"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
        "-framework CoreDisplay"
    )
    list(APPEND PLATFORM_LIBS ddcutil_macos)
elseif (CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    add_library(ddcutil_netbsd STATIC
        ddcutil_netbsd.c
        ddcutil_compat.h
    )
    list(APPEND PLATFORM_LIBS ddcutil_netbsd)
endif ()

add_executable(dimmitd
    dimmitd.c
    ddc.c
)
target_include_directories(dimmitd
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${DDCUTIL_INCLUDE_DIRS}
)
target_compile_options(dimmitd
    PRIVATE
    ${DDCUTIL_CFLAGS_OTHER}
)
target_link_libraries(dimmitd
    PRIVATE
    Threads::Threads
    ${PLATFORM_LIBS}
)

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
