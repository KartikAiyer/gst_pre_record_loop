#include <gst/gst.h>
#include <print>

int main(int argc, char *argv[]) {
    GstElement *pipeline, *src, *prerec, *sink;
    GstBus *bus;
    GstMessage *msg;
    GError *error = NULL;

    // Initialize GStreamer
    gst_init(&argc, &argv);

    // OPTIONAL: Add your plugin directory to the search path
    gst_plugin_load_file("build/Release/gstprerecordloop/libgstprerecordloop.so", NULL);

    // Build pipeline elements
    src = gst_element_factory_make("videotestsrc", "source");
    prerec = gst_element_factory_make("pre_record_loop", "prerecord");
    sink = gst_element_factory_make("fakesink", "sink");

    if (!src || !prerec || !sink) {
        g_printerr("Failed to create one or more elements!\n");
        return -1;
    }

    // Create empty pipeline
    pipeline = gst_pipeline_new("test-pipeline");
    if (!pipeline) {
        g_printerr("Failed to create pipeline!\n");
        return -1;
    }

    // Add elements and link them
    gst_bin_add_many(GST_BIN(pipeline), src, prerec, sink, NULL);
    if (!gst_element_link_many(src, prerec, sink, NULL)) {
        g_printerr("Failed to link elements!\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // Start playing
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Wait until error or EOS
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
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

    return 0;}
