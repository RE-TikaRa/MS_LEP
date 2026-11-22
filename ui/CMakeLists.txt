pkg_check_modules(DESKTOP REQUIRED IMPORTED_TARGET gtk+-3.0>=3.22)

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
add_executable(ms_lep main.c laser_console.c "${resource}")
target_link_libraries(ms_lep PRIVATE ms_lep_core PkgConfig::DESKTOP)
target_compile_definitions(ms_lep PRIVATE MS_LEP_VERSION="${PROJECT_VERSION}")
set_target_properties(ms_lep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
install(TARGETS ms_lep RUNTIME DESTINATION bin)
install(FILES "${resource}" DESTINATION bin)

target_compile_definitions(ms_lep PRIVATE
    GDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_22
    GDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_22
    GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56
    GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56
)
