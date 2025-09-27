# Tests CMakeLists: defines unit/integration/perf/memory tests

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST_CHECK REQUIRED IMPORTED_TARGET gstreamer-check-1.0)

# Shared test utility library
add_library(prerec_test_utils STATIC unit/test_utils.c)
target_link_libraries(prerec_test_utils PRIVATE PkgConfig::gstreamer)
target_include_directories(prerec_test_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unit)

# Unit Test: placeholder
add_executable(unit_test_placeholder unit/test_placeholder.c)
target_compile_features(unit_test_placeholder PRIVATE c_std_11)
target_include_directories(unit_test_placeholder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(unit_test_placeholder PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_placeholder COMMAND unit_test_placeholder)
set_tests_properties(prerec_unit_placeholder PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Unit Test: plugin registration (T009)
add_executable(unit_test_plugin_registration unit/test_plugin_registration.c)
target_compile_features(unit_test_plugin_registration PRIVATE c_std_11)
target_include_directories(unit_test_plugin_registration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(unit_test_plugin_registration PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_plugin_registration COMMAND unit_test_plugin_registration)
set_tests_properties(prerec_unit_plugin_registration PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Future: additional unit tests appended here (T010+)

# Unit Test: queue pruning invariants (T010 - expected failing)
add_executable(unit_test_queue_pruning unit/test_queue_pruning.c)
target_compile_features(unit_test_queue_pruning PRIVATE c_std_11)
target_include_directories(unit_test_queue_pruning PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
target_link_libraries(unit_test_queue_pruning PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_queue_pruning COMMAND unit_test_queue_pruning)
set_tests_properties(prerec_unit_queue_pruning PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}" )

# Unit Test: pruning keeps 2 GOP floor (T011 - expected failing)
add_executable(unit_test_pruning_two_gop_floor unit/test_pruning_two_gop_floor.c)
target_compile_features(unit_test_pruning_two_gop_floor PRIVATE c_std_11)
target_include_directories(unit_test_pruning_two_gop_floor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(unit_test_pruning_two_gop_floor PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_pruning_two_gop_floor COMMAND unit_test_pruning_two_gop_floor)
set_tests_properties(prerec_unit_pruning_two_gop_floor PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Unit Test: re-arm / flush trigger sequence (T012 - expected failing)
add_executable(unit_test_rearm_sequence unit/test_rearm_sequence.c)
target_compile_features(unit_test_rearm_sequence PRIVATE c_std_11)
target_include_directories(unit_test_rearm_sequence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(unit_test_rearm_sequence PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_rearm_sequence COMMAND unit_test_rearm_sequence)
set_tests_properties(prerec_unit_rearm_sequence PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Unit Test: flush on EOS behavior (T013 - expected failing)
add_executable(unit_test_flush_on_eos unit/test_flush_on_eos.c)
target_compile_features(unit_test_flush_on_eos PRIVATE c_std_11)
target_include_directories(unit_test_flush_on_eos PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(unit_test_flush_on_eos PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_unit_flush_on_eos COMMAND unit_test_flush_on_eos)
set_tests_properties(prerec_unit_flush_on_eos PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Integration Test: flush sequence (T014 - expected failing)
add_executable(integration_test_flush_sequence integration/test_flush_sequence.c)
target_compile_features(integration_test_flush_sequence PRIVATE c_std_11)
target_include_directories(integration_test_flush_sequence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(integration_test_flush_sequence PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_integration_flush_sequence COMMAND integration_test_flush_sequence)
set_tests_properties(prerec_integration_flush_sequence PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Integration Test: re-arm cycle (T015 - expected failing)
add_executable(integration_test_rearm_cycle integration/test_rearm_cycle.c)
target_compile_features(integration_test_rearm_cycle PRIVATE c_std_11)
target_include_directories(integration_test_rearm_cycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(integration_test_rearm_cycle PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_integration_rearm_cycle COMMAND integration_test_rearm_cycle)
set_tests_properties(prerec_integration_rearm_cycle PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Integration Test: oversize gop retention (T016 - expected failing)
add_executable(integration_test_oversize_gop integration/test_oversize_gop.c)
target_compile_features(integration_test_oversize_gop PRIVATE c_std_11)
target_include_directories(integration_test_oversize_gop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(integration_test_oversize_gop PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_integration_oversize_gop COMMAND integration_test_oversize_gop)
set_tests_properties(prerec_integration_oversize_gop PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Perf Test: latency prune skeleton (T017 - expected failing)
add_executable(perf_test_latency_prune perf/test_latency_prune.c)
target_compile_features(perf_test_latency_prune PRIVATE c_std_11)
target_include_directories(perf_test_latency_prune PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(perf_test_latency_prune PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
add_test(NAME prerec_perf_latency_prune COMMAND perf_test_latency_prune)
set_tests_properties(prerec_perf_latency_prune PROPERTIES
  ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")

# Memory Test: leak baseline script (T018 - expected failing)
add_test(NAME prerec_memory_leak_baseline COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/memory/test_leaks.sh)
