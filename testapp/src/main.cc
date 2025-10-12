#include <gst/gst.h>
#include <signal.h>

// Global pipeline pointer for signal handler
static GstElement* g_pipeline = nullptr;

// Structure to track frame count and trigger flush
typedef struct {
  guint       frame_count;
  GstElement* prerecordloop;
  gboolean    flush_sent;
} ProbeData;

// Signal handler for Ctrl-C
static void sigint_handler(int sig) {
  g_print("\nReceived interrupt signal. Sending EOS...\n");
  if (g_pipeline) {
    gst_element_send_event(g_pipeline, gst_event_new_eos());
  }
}

// Pad probe callback to count frames and trigger flush at frame 600
static GstPadProbeReturn frame_counter_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  ProbeData* data = (ProbeData*) user_data;

  // Only count buffers, not events
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
    data->frame_count++;

    // Send flush trigger after 600 frames (2/3 of 900)
    if (data->frame_count == 600 && !data->flush_sent) {
      g_print("Frame %u reached - Sending flush trigger to prerecordloop!\n", data->frame_count);

      // Create custom flush event
      GstEvent* flush_event =
          gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, gst_structure_new_empty("prerecord-flush"));

      // Send event to prerecordloop element
      if (gst_element_send_event(data->prerecordloop, flush_event)) {
        g_print("Flush event sent successfully!\n");
        data->flush_sent = TRUE;
      } else {
        g_printerr("Failed to send flush event!\n");
      }
    }

    // Log progress every 30 frames (1 second at 30fps)
    if (data->frame_count % 30 == 0) {
      g_print("Processed %u frames...\n", data->frame_count);
    }
  }

  return GST_PAD_PROBE_OK;
}

static GstElement* create_pipeline() {
  /* Probe for available H.264 encoders and pick the first available
   * priority list: vtenc_h264 (mac), v4l2h264enc, v4l2h264, x264enc
   */
  const char* candidates[] = {"vtenc_h264", "v4l2h264enc", "v4l2h264", "x264enc", NULL};
  const char* chosen = NULL;
  for (const char** c = candidates; *c != NULL; ++c) {
    GstElementFactory* factory = gst_element_factory_find(*c);
    if (factory) {
      chosen = *c;
      gst_object_unref(factory);
      break;
    }
  }

  if (!chosen) {
    g_printerr("No suitable H.264 encoder found. Tried vtenc_h264, v4l2h264enc, v4l2h264, x264enc.\n");
    return NULL;
  }

#ifdef AS_MP4
  gchar pipeline_desc[1024];
  g_snprintf(pipeline_desc, sizeof(pipeline_desc),
             "videotestsrc is-live=true ! "
             "capsfilter "
             "caps=video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! "
             "timeoverlay text=\"Stopwatch: \" shaded-background=true ! "
             "videoconvert ! %s ! "
             "h264parse ! "
             "mp4mux ! "
             "filesink location=output.mp4",
             chosen);
#else
  gchar pipeline_desc[1024];
  g_snprintf(pipeline_desc, sizeof(pipeline_desc),
             "videotestsrc is-live=true num-buffers=900 name=testsrc ! "
             "capsfilter "
             "caps=video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! "
             "timeoverlay text=\"Stopwatch: \" shaded-background=true ! "
             "videoconvert ! %s ! "
             "h264parse ! "
             "pre_record_loop name=prerecordloop ! "
             "qtmux ! "
             "filesink location=output_prerecord.mp4",
             chosen);
#endif

  g_print("Using encoder: %s\n", chosen);
  return gst_parse_launch(pipeline_desc, NULL);
}

int main(int argc, char* argv[]) {
  GstElement* pipeline;
  GstBus*     bus;
  GstMessage* msg;
  GError*     error = NULL;

    // Initialize GStreamer
  gst_init(&argc, &argv);

  // Create pipeline

  // Create pipeline
  pipeline = create_pipeline();
  if (!pipeline) {
    g_printerr("Failed to create pipeline!\n");
    return -1;
  }
  g_pipeline = pipeline; // Assign to global variable

  // Get reference to the prerecordloop element for property access and event sending
  GstElement* prerecordloop = NULL;
  GstElement* videotestsrc  = NULL;
  ProbeData   probe_data    = {0, NULL, FALSE};

#ifndef AS_MP4
  // Only get the element if we're using the prerecordloop pipeline
  prerecordloop = gst_bin_get_by_name(GST_BIN(pipeline), "prerecordloop");
  if (!prerecordloop) {
    g_printerr("Failed to get prerecordloop element from pipeline!\n");
    gst_object_unref(pipeline);
    return -1;
  }
  g_print("Successfully obtained prerecordloop element reference\n");

  // Get the videotestsrc element to attach probe
  videotestsrc = gst_bin_get_by_name(GST_BIN(pipeline), "testsrc");
  if (!videotestsrc) {
    g_printerr("Failed to get videotestsrc element from pipeline!\n");
    gst_object_unref(prerecordloop);
    gst_object_unref(pipeline);
    return -1;
  }
  g_print("Successfully obtained videotestsrc element reference\n");

  // Attach probe to videotestsrc's src pad to count frames
  GstPad* src_pad = gst_element_get_static_pad(videotestsrc, "src");
  if (!src_pad) {
    g_printerr("Failed to get src pad from videotestsrc!\n");
    gst_object_unref(videotestsrc);
    gst_object_unref(prerecordloop);
    gst_object_unref(pipeline);
    return -1;
  }

  // Initialize probe data
  probe_data.frame_count   = 0;
  probe_data.prerecordloop = prerecordloop;
  probe_data.flush_sent    = FALSE;

  // Add probe to count buffers
  gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, frame_counter_probe, &probe_data, NULL);
  g_print("Added frame counter probe to videotestsrc src pad\n");

  gst_object_unref(src_pad);
  gst_object_unref(videotestsrc);
#endif // Register signal handler for Ctrl-C
  g_print("Registered signal handler for Ctrl-C. Press Ctrl-C to stop recording.\n");
  signal(SIGINT, sigint_handler);

  // Start playing
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  // Wait until error or EOS
  bus = gst_element_get_bus(pipeline);
  msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType) (GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

  // Print error message if any
  if (msg != NULL) {
    GError* err;
    gchar*  debug_info;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error(msg, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
      g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error(&err);
      g_free(debug_info);
      break;
    case GST_MESSAGE_EOS:
      g_print("End-Of-Stream reached.\n");
      break;
    default:
      // Should not happen
      g_printerr("Unexpected message received.\n");
      break;
    }
    gst_message_unref(msg);
  }

  // Free resources
  gst_object_unref(bus);
#ifndef AS_MP4
  if (prerecordloop) {
    gst_object_unref(prerecordloop);
  }
#endif
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  return 0;
}
