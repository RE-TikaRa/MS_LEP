cmake_minimum_required(VERSION 3.22)

project(ms_lep VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0)

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
target_compile_options(ms_lep_core PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
)

option(MS_LEP_BUILD_GUI "Build the GTK4 application" ON)
if(MS_LEP_BUILD_GUI)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
    pkg_check_modules(ADW REQUIRED IMPORTED_TARGET libadwaita-1)

    add_executable(ms_lep
        src/main.c
        src/laser_console.c
    )
    target_link_libraries(ms_lep PRIVATE ms_lep_core PkgConfig::GTK4 PkgConfig::ADW)
    target_compile_definitions(ms_lep PRIVATE MS_LEP_VERSION="${PROJECT_VERSION}")
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
endif()

if(MS_LEP_BUILD_GUI)
    install(TARGETS ms_lep RUNTIME DESTINATION bin)
endif()
