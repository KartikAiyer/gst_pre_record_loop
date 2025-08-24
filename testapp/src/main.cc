#include <gst/gst.h>
#include <print>

static GstElement *create_pipeline() {
  const char *pipeline_desc =
      "videotestsrc ! "
      "capsfilter caps=video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! "
      "timeoverlay text=\"Stopwatch: \" shaded-background=true ! "
      "videoconvert ! "
      "vtenc_h264 ! "
      "pre_record_loop ! "
      "fakesink";
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
  gst_plugin_load_file("build/Release/gstprerecordloop/libgstprerecordloop.so",
                       NULL);

  // Create pipeline
  pipeline = create_pipeline();
  if (!pipeline) {
    g_printerr("Failed to create pipeline!\n");
    return -1;
  }

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
