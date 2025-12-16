cmake_minimum_required(VERSION 3.15)

# Detect if we're doing a macOS universal build
# Check both undefined and empty list (CLion sometimes sets empty)
list(LENGTH CMAKE_OSX_ARCHITECTURES NUM_ARCHS)
if (APPLE AND (NOT DEFINED CMAKE_OSX_ARCHITECTURES OR NUM_ARCHS EQUAL 0))
    # Top-level universal build for macOS
    project(dimmit-universal LANGUAGES C)
    include(ExternalProject)
    include(GNUInstallDirs)
    
    # Build arm64
    ExternalProject_Add(dimmit-arm64
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/build-arm64
        CMAKE_ARGS
            -DCMAKE_OSX_ARCHITECTURES=arm64
            -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        BUILD_ALWAYS 1
        INSTALL_COMMAND ""
    )
    
    # Build x86_64
    ExternalProject_Add(dimmit-x86_64
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64
        CMAKE_ARGS
            -DCMAKE_OSX_ARCHITECTURES=x86_64
            -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        BUILD_ALWAYS 1
        INSTALL_COMMAND ""
    )
    
    # Create universal binaries
    add_custom_target(dimmit-universal ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/universal
        COMMAND lipo -create
            ${CMAKE_CURRENT_BINARY_DIR}/build-arm64/dimmitd
            ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmitd
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmitd
        COMMAND lipo -create
            ${CMAKE_CURRENT_BINARY_DIR}/build-arm64/dimmit-up
            ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmit-up
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-up
        COMMAND lipo -create
            ${CMAKE_CURRENT_BINARY_DIR}/build-arm64/dimmit-down
            ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmit-down
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-down
        DEPENDS dimmit-arm64 dimmit-x86_64
        COMMENT "Creating universal binaries..."
        BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmitd
            ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-up
            ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-down
    )
    
    # Show lipo info
    add_custom_command(TARGET dimmit-universal POST_BUILD
        COMMAND lipo -info ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmitd
        COMMAND lipo -info ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-up
        COMMAND lipo -info ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-down
        COMMENT "Universal binary info:"
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
endif()

# ============================================================================
# Normal build (single-arch on all platforms, or arch-specific Darwin build)
# ============================================================================

project(dimmit LANGUAGES C OBJC)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckSymbolExists)
include(GNUInstallDirs)

# ============================================================================
# Platform Configuration
# ============================================================================

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

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
        add_library(ddcctl STATIC
            vendor/ddcctl/src/DDC.c
            vendor/ddcctl/src/ddcctl.m
        )
        target_include_directories(ddcctl PUBLIC vendor/ddcctl/src)
        target_compile_options(ddcctl PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-declarations>
            $<$<COMPILE_LANGUAGE:OBJC>:-Wno-deprecated-declarations>
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

    # Common macOS frameworks
    list(APPEND PLATFORM_LIBS
        "-framework IOKit"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
        "-framework CoreDisplay"
    )
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
            set_source_files_properties(
                ddc_impl_darwin_ddcctl.c
                PROPERTIES
                    COMPILE_OPTIONS "-Wno-deprecated-declarations"
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