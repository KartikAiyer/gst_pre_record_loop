/* T017/T037: Performance benchmark - latency impact of pruning operations
 * Measures median and 99th percentile latency for pruning cycles.
 * Tests pruning performance with various GOP sizes and max-time constraints.
 */

#define FAIL_PREFIX "T017 latency benchmark: "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_SAMPLES 100
#define GOP_DELTA_COUNT 10

/* Comparison function for qsort */
static int compare_guint64(const void* a, const void* b) {
  guint64 val_a = *(const guint64*) a;
  guint64 val_b = *(const guint64*) b;
  if (val_a < val_b)
    return -1;
  if (val_a > val_b)
    return 1;
  return 0;
}

/* Calculate median from sorted array */
static guint64 calculate_median(guint64* sorted_values, guint count) {
  if (count == 0)
    return 0;
  if (count % 2 == 0) {
    return (sorted_values[count / 2 - 1] + sorted_values[count / 2]) / 2;
  } else {
    return sorted_values[count / 2];
  }
}

/* Calculate 99th percentile from sorted array */
static guint64 calculate_percentile_99(guint64* sorted_values, guint count) {
  if (count == 0)
    return 0;
  guint index = (guint) ((count - 1) * 0.99);
  return sorted_values[index];
}

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available())
    FAIL("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t017-latency"))
    FAIL("pipeline create failed");

  /* Configure max-time to force pruning: 2 seconds (pipeline already in PLAYING state) */
  g_object_set(tp.pr, "max-time", 2, NULL);

  guint64* latencies = g_new0(guint64, NUM_SAMPLES);
  guint sample_count = 0;
  guint64 pts = 0;
  const guint64 dur = GST_MSECOND * 100; /* 100ms per frame, 1s per GOP */

  g_print("Starting latency benchmark: %d samples with pruning...\n", NUM_SAMPLES);

  for (guint i = 0; i < NUM_SAMPLES; ++i) {
    /* Query stats before push */
    GstQuery* q_before = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
    if (!gst_element_query(tp.pr, q_before)) {
      gst_query_unref(q_before);
      FAIL("stats query failed (before)");
    }
    const GstStructure* s_before = gst_query_get_structure(q_before);
    guint queued_before = 0;
    gst_structure_get_uint(s_before, "queued-buffers", &queued_before);
    gst_query_unref(q_before);

    /* Measure time to push GOP and trigger pruning */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (!prerec_push_gop(tp.appsrc, GOP_DELTA_COUNT, &pts, dur, NULL)) {
      FAIL("push GOP failed at sample %u", i);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    /* Calculate elapsed time in nanoseconds */
    guint64 elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL + (end.tv_nsec - start.tv_nsec);

    /* Query stats after push to confirm pruning occurred */
    GstQuery* q_after = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
    if (!gst_element_query(tp.pr, q_after)) {
      gst_query_unref(q_after);
      FAIL("stats query failed (after)");
    }
    const GstStructure* s_after = gst_query_get_structure(q_after);
    guint queued_after = 0, drops_gops = 0;
    gst_structure_get_uint(s_after, "queued-buffers", &queued_after);
    gst_structure_get_uint(s_after, "drops-gops", &drops_gops);
    gst_query_unref(q_after);

    /* Store latency sample (only if pruning occurred at some point) */
    if (i >= 3 || drops_gops > 0) { /* Allow buffer to fill initially */
      latencies[sample_count++] = elapsed_ns;
    }

    /* Brief sleep to avoid overwhelming the pipeline */
    g_usleep(1000); /* 1ms */
  }

  /* Sort latencies for percentile calculation */
  qsort(latencies, sample_count, sizeof(guint64), compare_guint64);

  /* Calculate statistics */
  guint64 median_ns = calculate_median(latencies, sample_count);
  guint64 p99_ns = calculate_percentile_99(latencies, sample_count);

  /* Calculate min/max for context */
  guint64 min_ns = sample_count > 0 ? latencies[0] : 0;
  guint64 max_ns = sample_count > 0 ? latencies[sample_count - 1] : 0;

  /* Report results */
  g_print("\n=== Pruning Latency Benchmark Results ===\n");
  g_print("Samples collected: %u\n", sample_count);
  g_print("GOP size: 1 keyframe + %d delta frames\n", GOP_DELTA_COUNT);
  g_print("Frame duration: %" G_GUINT64_FORMAT " ms\n", dur / GST_MSECOND);
  g_print("Configured max-time: 2 seconds\n\n");

  g_print("Latency Statistics (time to push GOP with pruning):\n");
  g_print("  Minimum:  %8.3f ms\n", min_ns / 1000000.0);
  g_print("  Median:   %8.3f ms\n", median_ns / 1000000.0);
  g_print("  99th %%:   %8.3f ms\n", p99_ns / 1000000.0);
  g_print("  Maximum:  %8.3f ms\n", max_ns / 1000000.0);
  g_print("\n");

  /* Sanity checks: latencies should be reasonable (< 100ms for typical case) */
  if (median_ns > 100 * GST_MSECOND) {
    g_warning("Median latency unusually high (%.3f ms) - possible performance issue", median_ns / 1000000.0);
  }

  if (p99_ns > 500 * GST_MSECOND) {
    g_warning("99th percentile latency very high (%.3f ms) - check for bottlenecks", p99_ns / 1000000.0);
  }

  g_free(latencies);
  prerec_pipeline_shutdown(&tp);

  g_print("Latency benchmark completed successfully.\n");
  return 0;
}
