# vendor/CMakeLists.txt

if (APPLE)
    #
    # m1ddc — Objective-C
    #
    if (NOT TARGET m1ddc)
        add_library(m1ddc STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/m1ddc/sources/i2c.m
            ${CMAKE_CURRENT_SOURCE_DIR}/m1ddc/sources/ioregistry.m
            ${CMAKE_CURRENT_SOURCE_DIR}/m1ddc/sources/m1ddc.m
        )

        target_include_directories(m1ddc PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/m1ddc/headers
        )

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
    endif()

    #
    # ddcctl — C + Objective-C
    #
    if (NOT TARGET ddcctl)
        add_library(ddcctl STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/ddcctl/src/DDC.c
            ${CMAKE_CURRENT_SOURCE_DIR}/ddcctl/src/ddcctl.m
        )

        target_include_directories(ddcctl PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/ddcctl/src
        )

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
    endif()
endif()
