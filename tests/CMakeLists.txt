# Tests CMakeLists: defines unit/integration/perf/memory tests

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST_CHECK REQUIRED IMPORTED_TARGET gstreamer-check-1.0)

# Unit Test: placeholder
add_executable(unit_test_placeholder unit/test_placeholder.c)
set_target_properties(unit_test_placeholder PROPERTIES C_STANDARD 11)
target_link_libraries(unit_test_placeholder PRIVATE PkgConfig::gstreamer)
add_test(NAME prerec_unit_placeholder COMMAND unit_test_placeholder)
set_tests_properties(prerec_unit_placeholder PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Unit Test: plugin registration (T009)
add_executable(unit_test_plugin_registration unit/test_plugin_registration.c)
set_target_properties(unit_test_plugin_registration PROPERTIES C_STANDARD 11)
target_link_libraries(unit_test_plugin_registration PRIVATE PkgConfig::gstreamer)
add_test(NAME prerec_unit_plugin_registration COMMAND unit_test_plugin_registration)
set_tests_properties(prerec_unit_plugin_registration PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Future: additional unit tests appended here (T010+)
