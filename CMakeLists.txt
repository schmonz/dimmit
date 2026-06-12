cmake_minimum_required(VERSION 3.15)

# The project VERSION below is a non-meaningful placeholder. Releases are
# versioned by git tag (see .github/workflows/release.yml), which sets the .pkg
# and latest.json version. Revisit if/when we adopt a real versioning scheme.
if(APPLE)
    project(dimmit VERSION 0.0.0 LANGUAGES C OBJC)
else()
    project(dimmit VERSION 0.0.0 LANGUAGES C)
endif()

# Detect if we're doing a macOS universal build
# Check both undefined and empty list (CLion sometimes sets empty)
list(LENGTH CMAKE_OSX_ARCHITECTURES NUM_ARCHS)
if (APPLE AND (NOT DEFINED CMAKE_OSX_ARCHITECTURES OR NUM_ARCHS EQUAL 0))
    # Top-level universal build for macOS.
    #
    # TODO(build): try moving this orchestration out of CMake into a thin
    # build_universal.sh (two per-arch builds + lipo + the arm64 probe), leaving
    # CMakeLists with a single per-arch mode and no ExternalProject. The per-arch
    # split itself is mandatory -- arm64 builds the m1ddc/CoreDisplay backend at
    # min 12.0 while x86_64 builds the ddcctl/IOKit backend at min 10.9 -- so a
    # script would only relocate the two-build orchestration, not collapse it to
    # one fat build. Deferred; the ExternalProject wrapper below works.
    project(dimmit-universal VERSION 0.0.0 LANGUAGES C OBJC)
    include(ExternalProject)
    include(GNUInstallDirs)

    # Probe whether this toolchain can actually emit an arm64 slice. The
    # Mavericks-era clang (Xcode 6) rejects both `-arch arm64` and a 12.0
    # deployment target ("invalid version number in '-mmacosx-version-min=12.0'"),
    # so on such hosts we fall back to an x86_64-only binary instead of failing
    # the entire universal build. Modern macOS toolchains pass the probe and get
    # a real fat binary.
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -arch arm64 -mmacosx-version-min=12.0
                -x c -c /dev/null -o /dev/null
        RESULT_VARIABLE DIMMIT_ARM64_PROBE
        OUTPUT_QUIET ERROR_QUIET
    )
    if (DIMMIT_ARM64_PROBE EQUAL 0)
        set(DIMMIT_BUILD_ARM64 TRUE)
        message(STATUS "Universal build: arm64 slice enabled")
    else()
        set(DIMMIT_BUILD_ARM64 FALSE)
        message(STATUS "Universal build: toolchain cannot target arm64; building x86_64 only")
    endif()

    # Build arm64 (only where the toolchain supports it).
    if (DIMMIT_BUILD_ARM64)
        ExternalProject_Add(dimmit-arm64
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
            BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/build-arm64
            CMAKE_ARGS
                -DCMAKE_OSX_ARCHITECTURES=arm64
                -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                -DFORCE_COREDISPLAY=ON
            # Drive the sub-build with the same cmake that's running this
            # top-level build (pkgsrc cmake on the Mavericks host, the runner's
            # cmake in CI) -- never a hardcoded path. BUILD_COMMAND is spelled
            # out so the sub-build's generator is invoked correctly.
            CMAKE_COMMAND ${CMAKE_COMMAND}
            LIST_SEPARATOR |
            BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR>
            BUILD_ALWAYS 1
            INSTALL_COMMAND ""
        )
    endif()

    # Build x86_64
    ExternalProject_Add(dimmit-x86_64
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64
        CMAKE_ARGS
            -DCMAKE_OSX_ARCHITECTURES=x86_64
            -DCMAKE_OSX_DEPLOYMENT_TARGET=10.9
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        CMAKE_COMMAND ${CMAKE_COMMAND}
        LIST_SEPARATOR |
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR>
        BUILD_ALWAYS 1
        INSTALL_COMMAND ""
    )
    
    # Stage the binaries. With only the x86_64 slice (e.g. on Mavericks), each
    # `lipo -create` on a single input just produces a thin Mach-O; when the
    # arm64 slice is also built, prepending it makes a real fat binary.
    set(DIMMIT_DAEMON_SLICES ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmitd)
    set(DIMMIT_UP_SLICES     ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmit-up)
    set(DIMMIT_DOWN_SLICES   ${CMAKE_CURRENT_BINARY_DIR}/build-x86_64/dimmit-down)
    set(DIMMIT_SLICE_DEPS    dimmit-x86_64)
    if (DIMMIT_BUILD_ARM64)
        list(PREPEND DIMMIT_DAEMON_SLICES ${CMAKE_CURRENT_BINARY_DIR}/build-arm64/dimmitd)
        list(PREPEND DIMMIT_UP_SLICES     ${CMAKE_CURRENT_BINARY_DIR}/build-arm64/dimmit-up)
        list(PREPEND DIMMIT_DOWN_SLICES   ${CMAKE_CURRENT_BINARY_DIR}/build-arm64/dimmit-down)
        list(APPEND  DIMMIT_SLICE_DEPS    dimmit-arm64)
    endif()

    add_custom_target(dimmit-universal ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/universal
        COMMAND lipo -create ${DIMMIT_DAEMON_SLICES}
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmitd
        COMMAND lipo -create ${DIMMIT_UP_SLICES}
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-up
        COMMAND lipo -create ${DIMMIT_DOWN_SLICES}
            -output ${CMAKE_CURRENT_BINARY_DIR}/universal/dimmit-down
        DEPENDS ${DIMMIT_SLICE_DEPS}
        COMMENT "Staging universal binaries..."
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
    project(dimmit VERSION 0.0.0 LANGUAGES C OBJC)
endif()

# ============================================================================
# Normal build (single-arch on all platforms, or arch-specific Darwin build)
# ============================================================================

# Declare the project for non-Apple platforms (C only). On Apple we already
# called project(... C OBJC) at line 78; re-declaring with C-only here would
# clobber the OBJC language and break the .m sources, so guard it.
if (NOT APPLE)
    project(dimmit VERSION 0.0.0 LANGUAGES C)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckSymbolExists)
include(GNUInstallDirs)

# ============================================================================
# Platform Configuration
# ============================================================================

find_package(Threads REQUIRED)

# dimmer.c calls lround(), which lives in libm on platforms (Linux, the BSDs)
# that split the math library out of libc. macOS folds libm into libSystem, so
# find_library still resolves there and the explicit link is a harmless no-op.
find_library(MATH_LIBRARY m)

# ============================================================================
# Build Configuration
# ============================================================================

set(DIMMIT_SOCK_DEFAULT "/tmp/dimmit.sock" CACHE STRING "Default Unix socket path for dimmit")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service/service_systemd.service
        ${CMAKE_CURRENT_BINARY_DIR}/service_systemd.service @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service/service_netbsd.sh
        ${CMAKE_CURRENT_BINARY_DIR}/service_netbsd.sh @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service/service_darwin.plist
        ${CMAKE_CURRENT_BINARY_DIR}/service_darwin.plist @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/service/service_darwin_install.sh
        ${CMAKE_CURRENT_BINARY_DIR}/service_darwin_install.sh @ONLY)

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
        set(DDC_ARCH_LIB m1ddc)

    elseif (BUILD_ARCH STREQUAL "x86_64")
        # On 10.9 ColorSync is a subframework of ApplicationServices, not a
        # top-level framework, so ddcctl's `#include <ColorSync/...>` only
        # resolves with this framework search path. (Harmless on newer macOS.)
        #
        # CMAKE_OSX_SYSROOT can be empty when CMake lets the compiler pick its
        # own default SDK (the case on a Tahoe / Apple-Silicon host driving the
        # x86_64 sub-build). An empty value would root the path below at the
        # *live* /System frameworks, which ship no Headers/. Worse, an -F there
        # binds clang's `CoreGraphics.framework` lookup to that headerless copy
        # and shadows the SDK's, breaking `#include <CoreGraphics/...>`. So fall
        # back to the active SDK path, which has the headers. On Mavericks
        # CMAKE_OSX_SYSROOT is set (or xcrun yields a 10.9 SDK that does have the
        # subframework headers, or stays empty -> the live path that works
        # there), so this is a no-op for that build.
        set(APPLESERVICES_SYSROOT "${CMAKE_OSX_SYSROOT}")
        if (NOT APPLESERVICES_SYSROOT)
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE APPLESERVICES_SYSROOT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
        endif()
        set(APPLESERVICES_FW_DIR
            "${APPLESERVICES_SYSROOT}/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks")

        add_library(ddcctl STATIC
            vendor/ddcctl/src/DDC.c
            vendor/ddcctl/src/ddcctl.m
            src/platform/ddc/darwin/compat_ioregistry.c
        )
        target_include_directories(ddcctl PUBLIC vendor/ddcctl/src)
        target_compile_options(ddcctl PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-declarations>
            $<$<COMPILE_LANGUAGE:OBJC>:-Wno-deprecated-declarations>
            # ColorSync is a *subframework* of ApplicationServices on 10.9, so
            # `#include <ColorSync/...>` only resolves via a framework search
            # path (-F); a plain -I/-isystem can't, because there's no literal
            # ColorSync/ directory -- the header lives in ColorSync.framework/
            # Headers/. Harmless on newer macOS (ColorSync is top-level there).
            $<$<COMPILE_LANGUAGE:C>:-F${APPLESERVICES_FW_DIR}>
            $<$<COMPILE_LANGUAGE:OBJC>:-F${APPLESERVICES_FW_DIR}>
            # Give DDC.c a correct prototype for IORegistryEntryCopyPath() so a
            # pre-10.11 implicit-int return doesn't truncate the CFStringRef.
            $<$<COMPILE_LANGUAGE:C>:-include>
            $<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ddc/darwin/compat_ioregistry.h>
        )
        target_link_libraries(ddcctl PUBLIC
            "-framework Foundation"
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework IOKit"
            "-framework ApplicationServices"
        )
        set(DDC_ARCH_LIB ddcctl)

    else()
        message(FATAL_ERROR "Unsupported architecture: ${BUILD_ARCH}")
    endif()

    # Common macOS frameworks.
    list(APPEND PLATFORM_LIBS
        "-framework IOKit"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
    )

    if (FORCE_COREDISPLAY OR NOT BUILD_ARCH STREQUAL "x86_64")
        list(APPEND PLATFORM_LIBS "-framework CoreDisplay")
    endif()

    # macOS < 10.12 has no clock_gettime() (used by dimmitd.c). We supply our
    # own (src/platform/compat/clock_gettime.c), self-gated on the deployment
    # target -- see that header. This replaces macports-legacy-support, whose
    # clock_gettime gates on the SDK version and so never activates on a
    # newer-SDK build targeting <10.12 (e.g. a Tahoe host building for 10.9).
endif()

# ============================================================================
# Main Executable
# ============================================================================

add_executable(dimmitd
    src/dimmitd.c
    src/dimmer.c
    src/command.c
    src/platform/ddc/abstraction.c
)

# Include paths:
#  - BINARY_DIR  : generated config.h
#  - SOURCE_DIR  : glue code references vendored headers by repo-relative path
#                  (e.g. "vendor/ddcctl/src/DDC.h")
#  - SOURCE_DIR/src : the ddc subsystem is addressed by layer, so callers write
#                  "ddc/abstraction.h" / "ddc/implementation.h" / "ddc/darwin/arch.h"
target_include_directories(dimmitd PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dimmitd PRIVATE Threads::Threads ${PLATFORM_LIBS})
if (MATH_LIBRARY)
    target_link_libraries(dimmitd PRIVATE ${MATH_LIBRARY})
endif()

# clock_gettime() compat for macOS < 10.12. The source self-gates on the
# deployment target (no-op at >= 10.12). The header is force-included so its
# rename takes effect before dimmitd.c sees the system <time.h>; see
# src/platform/compat/clock_gettime.h.
if (APPLE)
    target_sources(dimmitd PRIVATE src/platform/compat/clock_gettime.c)
    target_compile_options(dimmitd PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-include>
        $<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/src/platform/compat/clock_gettime.h>)
endif()

# ============================================================================
# Platform backends
# ============================================================================
# Each subsystem under src/platform/<subsystem> provides one backend source per
# OS, named <system>.c. This helper selects and adds the right one, failing the
# configure if this platform has no backend for that subsystem. (Shared by the
# ddc and access-control subsystems.)
string(TOLOWER "${CMAKE_SYSTEM_NAME}" SYSTEM_LOWER)

function(dimmit_add_platform_backend target subsystem)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/${subsystem}/${SYSTEM_LOWER}.c")
    if (NOT EXISTS "${_src}")
        message(FATAL_ERROR
            "No ${subsystem} backend for ${CMAKE_SYSTEM_NAME} (expected ${_src})")
    endif()
    target_sources(${target} PRIVATE "${_src}")
endfunction()

dimmit_add_platform_backend(dimmitd ddc)
dimmit_add_platform_backend(dimmitd access-control)
dimmit_add_platform_backend(dimmitd logging)
dimmit_add_platform_backend(dimmitd input)

# Platform-specific extras for the DDC backend (vendored libs, arch glue, header
# search paths). The access-control backends need none of this.
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(dimmitd PRIVATE src/platform/ddc/darwin/arch.h)

    # Add arch-specific glue code
    if (BUILD_ARCH STREQUAL "arm64")
        target_sources(dimmitd PRIVATE src/platform/ddc/darwin/m1ddc.m)
        # Match the vendored m1ddc lib: MRC, so a stack DisplayInfos (which holds
        # NSString* members) is allowed (ARC forbids objects in C structs).
        set_source_files_properties(
            src/platform/ddc/darwin/m1ddc.m
            PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
    elseif (BUILD_ARCH STREQUAL "x86_64")
        target_sources(dimmitd PRIVATE src/platform/ddc/darwin/ddcctl.c)
        # This file also includes ddcctl's DDC.h, so it needs the same
        # ColorSync-subframework search path as the ddcctl library.
        set_source_files_properties(
            src/platform/ddc/darwin/ddcctl.c
            PROPERTIES
                COMPILE_OPTIONS "-Wno-deprecated-declarations;-F${APPLESERVICES_FW_DIR}"
        )
    endif()

    target_link_libraries(dimmitd PRIVATE ${DDC_ARCH_LIB})

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(dimmitd PRIVATE ${DDCUTIL_INCLUDE_DIRS})
    target_compile_options(dimmitd PRIVATE ${DDCUTIL_CFLAGS_OTHER})
endif()

# ============================================================================
# Client Executables
# ============================================================================

# Same source, two executables: dimmit.c dispatches on argv[0] (its own basename),
# so the only difference between "dimmit-up" and "dimmit-down" is the program name.
add_executable(dimmit-up src/dimmit.c)
add_executable(dimmit-down src/dimmit.c)

target_include_directories(dimmit-up PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(dimmit-down PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================================
# Installation
# ============================================================================

install(TARGETS dimmitd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS dimmit-up dimmit-down RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ============================================================================
# Tests
# ============================================================================
# test_dimmit.c links the daemon's logic modules directly -- the pure state
# machine (dimmer.c), the command parser (command.c), the ddc abstraction
# (platform/ddc/abstraction.c) driven by the in-memory mock backend
# (platform/ddc/in_memory_mock.c), and the access-control mock
# (platform/access-control/mock.c) for the authorization test -- so no hardware,
# frameworks, real clock, or worker thread are involved. (No dimmitd.c here: the
# tested logic lives in modules now.) Run with `ctest` from the build directory
# (on macOS, from the arch sub-build, e.g. build/build-x86_64).
enable_testing()

add_executable(test_dimmit
    src/test_dimmit.c src/dimmer.c src/command.c
    src/platform/ddc/abstraction.c src/platform/ddc/in_memory_mock.c
    src/platform/access-control/mock.c)
target_include_directories(test_dimmit PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
# test_dimmit also compiles dimmer.c, so it needs libm for the same lround().
if (MATH_LIBRARY)
    target_link_libraries(test_dimmit PRIVATE ${MATH_LIBRARY})
endif()

add_test(NAME dimmit_unit COMMAND test_dimmit)
