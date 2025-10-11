# Tests CMakeLists: defines unit/integration/perf/memory tests

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST_CHECK REQUIRED IMPORTED_TARGET gstreamer-check-1.0)
pkg_check_modules(GST_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)

# Shared test utility library
add_library(prerec_test_utils STATIC unit/test_utils.c)
target_link_libraries(prerec_test_utils PRIVATE PkgConfig::gstreamer)
target_include_directories(prerec_test_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unit)

######################################
# Helper function to reduce repetition
######################################

function(prerec_add_gst_exec_test CATEGORY NAME SOURCE)
  # CATEGORY: unit | integration | perf
  # NAME: logical short name (e.g. plugin_registration)
  # SOURCE: relative path to source file
  set(target "${CATEGORY}_test_${NAME}")
  add_executable(${target} ${SOURCE})
  target_compile_features(${target} PRIVATE c_std_11)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
  target_link_libraries(${target} PRIVATE PkgConfig::gstreamer PkgConfig::GST_APP prerec_test_utils)
  # Compose test name with consistent prefix
  set(test_name "prerec_${CATEGORY}_${NAME}")
  add_test(NAME ${test_name} COMMAND ${target})
  set_tests_properties(${test_name} PROPERTIES
    ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}")
endfunction()

# Unit tests
prerec_add_gst_exec_test(unit placeholder unit/test_placeholder.c)
prerec_add_gst_exec_test(unit plugin_registration unit/test_plugin_registration.c)
prerec_add_gst_exec_test(unit queue_pruning unit/test_queue_pruning.c)      # T010
prerec_add_gst_exec_test(unit pruning_two_gop_floor unit/test_pruning_two_gop_floor.c) # T011
prerec_add_gst_exec_test(unit rearm_sequence unit/test_rearm_sequence.c)    # T012
prerec_add_gst_exec_test(unit flush_on_eos unit/test_flush_on_eos.c)        # T013
prerec_add_gst_exec_test(unit flush_on_eos_policies unit/test_flush_on_eos_policies.c) # T025 extended
prerec_add_gst_exec_test(unit max_time_property unit/test_max_time_property.c) # T019
prerec_add_gst_exec_test(unit flush_trigger_name unit/test_flush_trigger_name.c) # T020a
prerec_add_gst_exec_test(unit no_refcount_critical unit/test_no_refcount_critical.c) # regression: ensure no refcount CRITICAL
prerec_add_gst_exec_test(unit concurrent_flush_ignore unit/test_concurrent_flush_ignore.c) # T022
prerec_add_gst_exec_test(unit stats_counters unit/test_stats_counters.c) # T026
prerec_add_gst_exec_test(unit seek_passthrough unit/test_seek_passthrough.c) # T031
prerec_add_gst_exec_test(unit gap_events unit/test_gap_events.c) # T032
prerec_add_gst_exec_test(unit sticky_events unit/test_sticky_events.c) # T033
prerec_add_gst_exec_test(unit flush_seek_reset unit/test_flush_seek_reset.c) # T034a

# Integration tests
prerec_add_gst_exec_test(integration flush_sequence integration/test_flush_sequence.c) # T014
prerec_add_gst_exec_test(integration rearm_cycle integration/test_rearm_cycle.c)       # T015
prerec_add_gst_exec_test(integration oversize_gop integration/test_oversize_gop.c)     # T016

# Perf tests
prerec_add_gst_exec_test(perf latency_prune perf/test_latency_prune.c)                 # T017

# Memory Test: leak baseline script (T018 - expected failing)
add_test(NAME prerec_memory_leak_baseline COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/memory/test_leaks.sh)
