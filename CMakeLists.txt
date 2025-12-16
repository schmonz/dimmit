cmake_minimum_required(VERSION 3.15)
project(dimmit LANGUAGES C OBJC)

add_subdirectory(vendor)

# Minimum macOS 12 required for kIOMainPortDefault API
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

    # For now, prefer single-arch builds. CLion/CMake may leave CMAKE_OSX_ARCHITECTURES empty.
    set(DIMMIT_DARWIN_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    if (DIMMIT_DARWIN_ARCH STREQUAL "")
        set(DIMMIT_DARWIN_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    # Normalize common host values to CMake arch names
    if (DIMMIT_DARWIN_ARCH STREQUAL "aarch64")
        set(DIMMIT_DARWIN_ARCH "arm64")
    elseif (DIMMIT_DARWIN_ARCH STREQUAL "amd64")
        set(DIMMIT_DARWIN_ARCH "x86_64")
    endif()

    # If an old cache value is a list like "arm64;x86_64", pick the first entry.
    #if (DIMMIT_DARWIN_ARCH MATCHES ";")
endif()

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
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_sources(dimmitd PRIVATE
            ddc_impl_darwin_arch.h
        )

        if (DIMMIT_DARWIN_ARCH STREQUAL "arm64")
            target_sources(dimmitd PRIVATE
                ddc_impl_darwin_m1ddc.m
            )
            target_link_libraries(dimmitd PRIVATE
                m1ddc
            )
        elseif (DIMMIT_DARWIN_ARCH STREQUAL "x86_64")
            target_sources(dimmitd PRIVATE
                ddc_impl_darwin_ddcctl.c
            )
            target_link_libraries(dimmitd PRIVATE
                ddcctl
            )
        else()
            message(WARNING "dimmit: Unknown macOS arch '${DIMMIT_DARWIN_ARCH}', defaulting to arm64 backend selection")
            target_sources(dimmitd PRIVATE
                ddc_impl_darwin_m1ddc.m
            )
            target_link_libraries(dimmitd PRIVATE
                m1ddc
            )
        endif()
    endif ()
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
