#include <gst/gst.h>
#include <print>
#include <signal.h>

// Global pipeline pointer for signal handler
static GstElement *g_pipeline = nullptr;

// Signal handler for Ctrl-C
static void sigint_handler(int sig) {
  g_print("\nReceived interrupt signal. Sending EOS...\n");
  if (g_pipeline) {
    gst_element_send_event(g_pipeline, gst_event_new_eos());
  }
}

static GstElement *create_pipeline()
{
  const char *pipeline_desc =
#ifdef AS_MP4
      "videotestsrc is-live=true ! "
      "capsfilter "
      "caps=video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! "
      "timeoverlay text=\"Stopwatch: \" shaded-background=true ! "
      "videoconvert ! "
      "vtenc_h264 ! "
      "h264parse ! "
      "mp4mux ! "
      "filesink location=output.mp4";
#else
      "videotestsrc is-live=true num-buffers=420 ! "
      "capsfilter "
      "caps=video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! "
      "timeoverlay text=\"Stopwatch: \" shaded-background=true ! "
      "videoconvert ! "
      "vtenc_h264 ! "
      "h264parse ! "
      "pre_record_loop ! "
      "fakesink";
#endif
      return gst_parse_launch(pipeline_desc, nullptr);
}

int main(int argc, char *argv[]) {
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;
  GError *error = NULL;

  // Initialize GStreamer
  gst_init(&argc, &argv);

  // OPTIONAL: Add your plugin directory to the search path
  GError *plugin_error = NULL;
  if (!gst_plugin_load_file("/Users/kartikaiyer/fun/gst_my_filter/build/Debug/gstprerecordloop/libgstprerecordloop.so",
                           &plugin_error)) {
    g_printerr("Failed to load plugin: %s\n", plugin_error ? plugin_error->message : "Unknown error");
    if (plugin_error) g_error_free(plugin_error);
    return -1;
  }
  g_print("Plugin loaded successfully\n");

  // Create pipeline
  pipeline = create_pipeline();
  if (!pipeline) {
    g_printerr("Failed to create pipeline!\n");
    return -1;
  }
  g_pipeline = pipeline; // Assign to global variable

  // Register signal handler for Ctrl-C
  g_print("Registered signal handler for Ctrl-C. Press Ctrl-C to stop recording.\n");
  signal(SIGINT, sigint_handler);

  // Start playing
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  // Wait until error or EOS
  bus = gst_element_get_bus(pipeline);
  msg = gst_bus_timed_pop_filtered(
      bus, GST_CLOCK_TIME_NONE,
      (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

  // Print error message if any
  if (msg != NULL) {
    GError *err;
    gchar *debug_info;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error(msg, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n",
                 GST_OBJECT_NAME(msg->src), err->message);
      g_printerr("Debugging information: %s\n",
                 debug_info ? debug_info : "none");
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
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  return 0;
}
