# Tests CMakeLists: defines unit/integration/perf/memory tests

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST_CHECK REQUIRED IMPORTED_TARGET gstreamer-check-1.0)

# Shared test utility library
add_library(prerec_test_utils STATIC unit/test_utils.c)
target_link_libraries(prerec_test_utils PRIVATE PkgConfig::gstreamer)
target_include_directories(prerec_test_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unit)

# Unit Test: placeholder
add_executable(unit_test_placeholder unit/test_placeholder.c)
set_target_properties(unit_test_placeholder PROPERTIES C_STANDARD 11)
target_link_libraries(unit_test_placeholder PRIVATE PkgConfig::gstreamer prerec_test_utils)
add_test(NAME prerec_unit_placeholder COMMAND unit_test_placeholder)
set_tests_properties(prerec_unit_placeholder PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Unit Test: plugin registration (T009)
add_executable(unit_test_plugin_registration unit/test_plugin_registration.c)
set_target_properties(unit_test_plugin_registration PROPERTIES C_STANDARD 11)
target_link_libraries(unit_test_plugin_registration PRIVATE PkgConfig::gstreamer prerec_test_utils)
add_test(NAME prerec_unit_plugin_registration COMMAND unit_test_plugin_registration)
set_tests_properties(prerec_unit_plugin_registration PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Future: additional unit tests appended here (T010+)

# Unit Test: queue pruning invariants (T010 - expected failing)
add_executable(unit_test_queue_pruning unit/test_queue_pruning.c)
set_target_properties(unit_test_queue_pruning PROPERTIES C_STANDARD 11)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
target_link_libraries(unit_test_queue_pruning PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_queue_pruning COMMAND unit_test_queue_pruning)
set_tests_properties(prerec_unit_queue_pruning PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}" )

# Unit Test: pruning keeps 2 GOP floor (T011 - expected failing)
add_executable(unit_test_pruning_two_gop_floor unit/test_pruning_two_gop_floor.c)
set_target_properties(unit_test_pruning_two_gop_floor PROPERTIES C_STANDARD 11)
target_link_libraries(unit_test_pruning_two_gop_floor PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_pruning_two_gop_floor COMMAND unit_test_pruning_two_gop_floor)
set_tests_properties(prerec_unit_pruning_two_gop_floor PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Unit Test: re-arm / flush trigger sequence (T012 - expected failing)
add_executable(unit_test_rearm_sequence unit/test_rearm_sequence.c)
set_target_properties(unit_test_rearm_sequence PROPERTIES C_STANDARD 11)
target_link_libraries(unit_test_rearm_sequence PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_rearm_sequence COMMAND unit_test_rearm_sequence)
set_tests_properties(prerec_unit_rearm_sequence PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Unit Test: flush on EOS behavior (T013 - expected failing)
add_executable(unit_test_flush_on_eos unit/test_flush_on_eos.c)
set_target_properties(unit_test_flush_on_eos PROPERTIES C_STANDARD 11)
target_link_libraries(unit_test_flush_on_eos PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_flush_on_eos COMMAND unit_test_flush_on_eos)
set_tests_properties(prerec_unit_flush_on_eos PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")
