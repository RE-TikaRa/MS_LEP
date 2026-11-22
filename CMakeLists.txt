cmake_minimum_required(VERSION 3.10)
project(ms_lep VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.56 gio-2.0>=2.56)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(ms_lep_core src/jpt_protocol.c src/jpt_device.c)
target_include_directories(ms_lep_core PUBLIC src)
target_link_libraries(ms_lep_core PUBLIC PkgConfig::GLIB)
target_compile_definitions(ms_lep_core PUBLIC
    GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56
    GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56
)
if(UNIX)
    target_sources(ms_lep_core PRIVATE src/jpt_transport.c src/jpt_client.c)
endif()

if(BUILD_TESTING)
    add_executable(ms_lep_protocol_test tests/protocol_test.c)
    target_link_libraries(ms_lep_protocol_test PRIVATE ms_lep_core)
    add_test(NAME protocol COMMAND ms_lep_protocol_test)

    if(UNIX)
        add_executable(ms_lep_transport_test tests/transport_test.c)
        target_link_libraries(ms_lep_transport_test PRIVATE ms_lep_core)
        add_test(NAME transport COMMAND ms_lep_transport_test)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(MS_LEP_PYTHON python3)
            if(NOT MS_LEP_PYTHON)
                message(FATAL_ERROR "Linux serial integration tests require Python 3.6 or newer")
            endif()
            add_executable(ms_lep_serial_client_test tests/serial_client_test.c)
            target_link_libraries(ms_lep_serial_client_test PRIVATE ms_lep_core)
            add_test(NAME virtual_serial COMMAND "${MS_LEP_PYTHON}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/virtual_serial_test.py"
                $<TARGET_FILE:ms_lep_serial_client_test> -v)
            set_tests_properties(virtual_serial PROPERTIES TIMEOUT 90)
        endif()
    endif()
endif()
