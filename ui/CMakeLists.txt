pkg_check_modules(DESKTOP REQUIRED IMPORTED_TARGET gtk4>=4.10 libadwaita-1>=1.5)

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
if(NOT GLIB_COMPILE_RESOURCES)
    message(FATAL_ERROR "glib-compile-resources is required to build the GUI")
endif()

set(resource ${CMAKE_BINARY_DIR}/ms_lep.gresource)
add_custom_command(OUTPUT "${resource}"
    COMMAND "${GLIB_COMPILE_RESOURCES}" --generate "--target=${resource}"
        "--sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data" "${CMAKE_CURRENT_SOURCE_DIR}/data/ms_lep.gresource.xml"
    DEPENDS data/ms_lep.gresource.xml data/laser-device.png
    VERBATIM
)
add_executable(ms_lep main.c laser_console.c alarm_replay.c "${resource}")
target_link_libraries(ms_lep PRIVATE ms_lep_core PkgConfig::DESKTOP)
target_compile_definitions(ms_lep PRIVATE MS_LEP_VERSION="${PROJECT_VERSION}")
set_target_properties(ms_lep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
install(TARGETS ms_lep RUNTIME DESTINATION bin)
install(FILES "${resource}" DESTINATION bin)

if(BUILD_TESTING)
    add_executable(ms_lep_replay_test test_alarm_replay.c alarm_replay.c)
    target_link_libraries(ms_lep_replay_test PRIVATE ms_lep_core PkgConfig::DESKTOP)
    add_test(NAME alarm_replay COMMAND ms_lep_replay_test)
    set_tests_properties(alarm_replay PROPERTIES SKIP_RETURN_CODE 77)
endif()
