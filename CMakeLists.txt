cmake_minimum_required(VERSION 3.15)

if(APPLE)
    project(MyProject LANGUAGES C OBJC)
else()
    project(MyProject LANGUAGES C)
endif()

# Detect if we're doing a macOS universal build
# Check both undefined and empty list (CLion sometimes sets empty)
list(LENGTH CMAKE_OSX_ARCHITECTURES NUM_ARCHS)
if (APPLE AND (NOT DEFINED CMAKE_OSX_ARCHITECTURES OR NUM_ARCHS EQUAL 0))
    # Top-level universal build for macOS
    project(dimmit-universal LANGUAGES C OBJC)
    include(ExternalProject)
    include(GNUInstallDirs)
    
# XXX just x86_64 when on Mavs
    # Build arm64
    #    ExternalProject_Add(dimmit-arm64
    #        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    #        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/build-arm64
    #        CMAKE_ARGS
    #            -DCMAKE_OSX_ARCHITECTURES=arm64
    #            -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
    #            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    #        BUILD_ALWAYS 1
    #        INSTALL_COMMAND ""
    #    )
    
    # Build x86_64
    ExternalProject_Add(dimmit-x86_64
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64
        CMAKE_ARGS
            -DCMAKE_OSX_ARCHITECTURES=x86_64
            -DCMAKE_OSX_DEPLOYMENT_TARGET=10.9
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        BUILD_ALWAYS 1
        INSTALL_COMMAND ""
    )
    
    # Stage the binaries. On Mavericks this is x86_64-only (arm64 is disabled
    # above), so `lipo -create` with a single input just produces a thin Mach-O;
    # re-enabling the arm64 ExternalProject restores a real fat binary.
    add_custom_target(dimmit-universal ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/universal
        COMMAND lipo -create
            ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmitd
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmitd
        COMMAND lipo -create
            ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmit-up
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-up
        COMMAND lipo -create
            ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmit-down
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-down
        DEPENDS dimmit-x86_64
        COMMENT "Staging x86_64 binaries..."
        BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmitd
            ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-up
            ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-down
    )
    
    # Install universal binaries
    install(PROGRAMS
        ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmitd
        DESTINATION ${CMAKE_INSTALL_SBINDIR}
    )
    install(PROGRAMS
        ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-up
        ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-down
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    
    return()
elseif(APPLE)
    project(dimmit LANGUAGES C OBJC)
endif()

# ============================================================================
# Normal build (single-arch on all platforms, or arch-specific Darwin build)
# ============================================================================

# XXX avoid OBJC on non-Apple platforms
project(dimmit LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckSymbolExists)
include(GNUInstallDirs)

# ============================================================================
# Platform Configuration
# ============================================================================

find_package(Threads REQUIRED)

# ============================================================================
# Build Configuration
# ============================================================================

set(DIMMIT_SOCK_DEFAULT "/tmp/dimmit.sock" CACHE STRING "Default Unix socket path for dimmit")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service_systemd.service
        ${CMAKE_CURRENT_BINARY_DIR}/service_systemd.service @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service_netbsd.sh
        ${CMAKE_CURRENT_BINARY_DIR}/service_netbsd.sh @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service_darwin.plist
        ${CMAKE_CURRENT_BINARY_DIR}/service_darwin.plist @ONLY)

# ============================================================================
# Platform-Specific Libraries
# ============================================================================

set(PLATFORM_LIBS)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND PLATFORM_LIBS rt)
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(DDCUTIL ddcutil)
    endif()
    if (NOT DDCUTIL_FOUND)
        message(FATAL_ERROR "ddcutil not found. Please install libddcutil development files.")
    endif()
    list(APPEND PLATFORM_LIBS ${DDCUTIL_LIBRARIES})

elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Get the architecture we're building for
    list(LENGTH CMAKE_OSX_ARCHITECTURES NUM_ARCHS)
    if (NUM_ARCHS EQUAL 0)
        message(FATAL_ERROR "CMAKE_OSX_ARCHITECTURES is empty. This shouldn't happen in arch-specific builds.")
    endif()
    list(GET CMAKE_OSX_ARCHITECTURES 0 BUILD_ARCH)
    message(STATUS "Building for macOS ${BUILD_ARCH}")

    # Build arch-specific vendored library
    if (BUILD_ARCH STREQUAL "arm64")
        add_library(m1ddc STATIC
            vendor/m1ddc/sources/i2c.m
            vendor/m1ddc/sources/ioregistry.m
            vendor/m1ddc/sources/m1ddc.m
        )
        target_include_directories(m1ddc PUBLIC vendor/m1ddc/headers)
        target_compile_options(m1ddc PRIVATE
            $<$<COMPILE_LANGUAGE:OBJC>:-fmodules>
            $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>
        )
        target_link_libraries(m1ddc PUBLIC
            "-framework Foundation"
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework IOKit"
        )
        set(DDC_VENDOR_LIB m1ddc)

    elseif (BUILD_ARCH STREQUAL "x86_64")
        # On 10.9 ColorSync is a subframework of ApplicationServices, not a
        # top-level framework, so ddcctl's `#include <ColorSync/...>` only
        # resolves with this framework search path. (Harmless on newer macOS.)
        set(APPLESERVICES_FW_DIR
            "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks")

        add_library(ddcctl STATIC
            vendor/ddcctl/src/DDC.c
            vendor/ddcctl/src/ddcctl.m
            compat_ioregistry_darwin.c
        )
        target_include_directories(ddcctl PUBLIC vendor/ddcctl/src)
        target_compile_options(ddcctl PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-declarations>
            $<$<COMPILE_LANGUAGE:OBJC>:-Wno-deprecated-declarations>
            $<$<COMPILE_LANGUAGE:C>:-F${APPLESERVICES_FW_DIR}>
            $<$<COMPILE_LANGUAGE:OBJC>:-F${APPLESERVICES_FW_DIR}>
            # Give DDC.c a correct prototype for IORegistryEntryCopyPath() so a
            # pre-10.11 implicit-int return doesn't truncate the CFStringRef.
            $<$<COMPILE_LANGUAGE:C>:-include>
            $<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/compat_ioregistry_darwin.h>
        )
        target_link_libraries(ddcctl PUBLIC
            "-framework Foundation"
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework IOKit"
            "-framework ApplicationServices"
        )
        set(DDC_VENDOR_LIB ddcctl)

    else()
        message(FATAL_ERROR "Unsupported architecture: ${BUILD_ARCH}")
    endif()

    # Common macOS frameworks.
    # NOTE: CoreDisplay (10.10+) is intentionally omitted -- it does not exist
    # on 10.9 and nothing here references it.
    list(APPEND PLATFORM_LIBS
        "-framework IOKit"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
    )

    # macOS < 10.12 has no clock_gettime() (used by dimmitd.c). Lean on
    # macports-legacy-support (pkgsrc/MacPorts) for the missing libc pieces.
    include(CheckSymbolExists)
    check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)
    if (NOT HAVE_CLOCK_GETTIME)
        find_path(LEGACYSUPPORT_INCLUDE_DIR
            NAMES MacportsLegacySupport.h
            PATH_SUFFIXES LegacySupport
            HINTS /opt/pkg/include /usr/pkg/include /opt/local/include)
        find_library(LEGACYSUPPORT_LIB
            NAMES MacportsLegacySupport
            HINTS /opt/pkg/lib /usr/pkg/lib /opt/local/lib)
        if (NOT LEGACYSUPPORT_INCLUDE_DIR OR NOT LEGACYSUPPORT_LIB)
            message(FATAL_ERROR
                "This macOS lacks clock_gettime() and macports-legacy-support "
                "was not found. Install it (pkgsrc: macports-legacy-support) "
                "or build on macOS 10.12+.")
        endif()
        message(STATUS "Using macports-legacy-support: ${LEGACYSUPPORT_LIB}")
    endif()
endif()

# ============================================================================
# Main Executable
# ============================================================================

add_executable(dimmitd
    dimmitd.c
    ddc.c
)

target_include_directories(dimmitd PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(dimmitd PRIVATE Threads::Threads ${PLATFORM_LIBS})

# macports-legacy-support for clock_gettime() etc. on macOS < 10.12.
# Header dir goes BEFORE the system include path so its overrides win.
if (LEGACYSUPPORT_INCLUDE_DIR)
    target_include_directories(dimmitd BEFORE PRIVATE ${LEGACYSUPPORT_INCLUDE_DIR})
    target_link_libraries(dimmitd PRIVATE ${LEGACYSUPPORT_LIB})
endif()

# Platform-specific DDC implementation
string(TOLOWER "${CMAKE_SYSTEM_NAME}" SYSTEM_LOWER)
set(DDC_IMPL_SRC "ddc_impl_${SYSTEM_LOWER}.c")

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${DDC_IMPL_SRC}")
    target_sources(dimmitd PRIVATE ${DDC_IMPL_SRC})
    
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_sources(dimmitd PRIVATE ddc_impl_darwin_arch.h)
        
        # Add arch-specific glue code
        if (BUILD_ARCH STREQUAL "arm64")
            target_sources(dimmitd PRIVATE ddc_impl_darwin_m1ddc.m)
        elseif (BUILD_ARCH STREQUAL "x86_64")
            target_sources(dimmitd PRIVATE ddc_impl_darwin_ddcctl.c)
            # This file also includes ddcctl's DDC.h, so it needs the same
            # ColorSync-subframework search path as the ddcctl library.
            set_source_files_properties(
                ddc_impl_darwin_ddcctl.c
                PROPERTIES
                    COMPILE_OPTIONS "-Wno-deprecated-declarations;-F${APPLESERVICES_FW_DIR}"
            )
        endif()
        
        target_link_libraries(dimmitd PRIVATE ${DDC_VENDOR_LIB})
    endif()

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(dimmitd PRIVATE ${DDCUTIL_INCLUDE_DIRS})
    target_compile_options(dimmitd PRIVATE ${DDCUTIL_CFLAGS_OTHER})
    
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME} (Expected ${DDC_IMPL_SRC})")
endif()

# ============================================================================
# Client Executables
# ============================================================================

add_executable(dimmit-up dimmit.c)
add_executable(dimmit-down dimmit.c)

target_include_directories(dimmit-up PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(dimmit-down PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================================
# Installation
# ============================================================================

install(TARGETS dimmitd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS dimmit-up dimmit-down RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
