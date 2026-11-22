cmake_minimum_required(VERSION 3.10)

project(ms_lep VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.56 gio-2.0>=2.56)

set(MS_LEP_CORE_SOURCES
    src/jpt_protocol.c
    src/jpt_device.c
)
if(UNIX)
    list(APPEND MS_LEP_CORE_SOURCES
        src/jpt_transport.c
        src/jpt_client.c
    )
endif()
add_library(ms_lep_core ${MS_LEP_CORE_SOURCES})
target_include_directories(ms_lep_core PUBLIC src)
target_link_libraries(ms_lep_core PUBLIC PkgConfig::GLIB)
target_compile_definitions(ms_lep_core PUBLIC
    GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56
    GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56
)
target_compile_options(ms_lep_core PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
)

option(MS_LEP_BUILD_GUI "Build the GTK3 application" ON)
if(MS_LEP_BUILD_GUI)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.22)
    find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
    if(NOT GLIB_COMPILE_RESOURCES)
        message(FATAL_ERROR "glib-compile-resources is required to build the GUI")
    endif()

    set(MS_LEP_RESOURCE_FILE ${CMAKE_CURRENT_BINARY_DIR}/ms_lep.gresource)
    add_custom_command(
        OUTPUT ${MS_LEP_RESOURCE_FILE}
        COMMAND ${GLIB_COMPILE_RESOURCES}
                --generate
                --target=${MS_LEP_RESOURCE_FILE}
                --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data
                ${CMAKE_CURRENT_SOURCE_DIR}/data/ms_lep.gresource.xml
        DEPENDS
            data/ms_lep.gresource.xml
            data/laser-device.png
        VERBATIM
    )
    add_custom_target(ms_lep_resources DEPENDS ${MS_LEP_RESOURCE_FILE})

    add_executable(ms_lep
        src/main.c
        src/laser_console.c
    )
    add_dependencies(ms_lep ms_lep_resources)
    target_link_libraries(ms_lep PRIVATE ms_lep_core PkgConfig::GTK3)
    target_compile_definitions(ms_lep PRIVATE MS_LEP_VERSION="${PROJECT_VERSION}"
        GDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_22
        GDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_22
    )
    target_compile_options(ms_lep PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
    )
endif()

if(BUILD_TESTING)
    add_executable(ms_lep_protocol_test tests/protocol_test.c)
    target_link_libraries(ms_lep_protocol_test PRIVATE ms_lep_core)
    target_compile_options(ms_lep_protocol_test PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME protocol COMMAND ms_lep_protocol_test)

    if(UNIX)
        add_executable(ms_lep_transport_test tests/transport_test.c)
        target_link_libraries(ms_lep_transport_test PRIVATE ms_lep_core)
        target_compile_options(ms_lep_transport_test PRIVATE
            $<$<C_COMPILER_ID:MSVC>:/W4>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
        )
        add_test(NAME transport COMMAND ms_lep_transport_test)
    endif()
endif()

if(MS_LEP_BUILD_GUI)
    install(TARGETS ms_lep RUNTIME DESTINATION bin)
    install(FILES ${MS_LEP_RESOURCE_FILE} DESTINATION bin)
endif()
