#include <gst/gst.h>

#include <stdio.h>

static GMainLoop *loop;

static void cb_need_data(GstElement *appsrc, guint unused_size, gpointer user_data) {
    static gboolean white = FALSE;
    static GstClockTime timestamp = 0;
    GstBuffer *buffer;
    guint size;
    GstFlowReturn ret;

    size = 480 * 320 * 2;//385 * 288 * 2;

    buffer = gst_buffer_new_allocate (NULL, size, NULL);

    /* this makes the image black/white */
    gst_buffer_memset (buffer, 0, white ? 0xff : 0x0, size);
    
    white = !white;

    GST_BUFFER_PTS (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 2);

    timestamp += GST_BUFFER_DURATION (buffer);

    g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);

    if (ret != GST_FLOW_OK) {
        /* something wrong, stop pushing */
        g_main_loop_quit (loop);
    }

    g_print("Hello!");
}

static gboolean bus_message(GstBus *bus, GstMessage *message, gpointer user_data) {
  GST_DEBUG ("got message %s",
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR: {
        GError *err = NULL;
        gchar *dbg_info = NULL;

        gst_message_parse_error (message, &err, &dbg_info);
        g_printerr ("ERROR from element %s: %s\n",
            GST_OBJECT_NAME (message->src), err->message);
        g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
        g_error_free (err);
        g_free (dbg_info);
        //g_main_loop_quit (app->loop);
        break;
    }
    case GST_MESSAGE_EOS:
      //g_main_loop_quit (app->loop);
      break;
    default:
      break;
  }
  return TRUE;
}


gint main(gint argc, gchar *argv[]) {
    GstElement *pipeline, *appsrc, *conv, *videosink;

    /* init GStreamer */
    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    /* setup pipeline */
    // pipeline = gst_pipeline_new("pipeline");
    // appsrc = gst_element_factory_make("appsrc", "source");
    // conv = gst_element_factory_make("videoconvert", "conv");
    // videosink = gst_element_factory_make("xvimagesink", "videosink");
    //pipeline = gst_parse_launch("appsrc name=mysource ! videoscale ! videoconvert ! video/x-raw,format=YUY2,width=640,height=480 ! xvimagesink", NULL);
    pipeline = gst_parse_launch("appsrc name=mysource ! xvimagesink", NULL);
    //pipeline = gst_parse_launch("appsrc name=mysource ! videoscale ! videoconvert ! video/x-raw,format=YUY2,width=640,height=480 ! v4l2sink device=/dev/video1", NULL);
    appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "mysource");


    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    gst_bus_add_watch (bus, (GstBusFunc) bus_message, NULL);


    /* setup */
    g_object_set(G_OBJECT(appsrc), "caps",
                    gst_caps_new_simple("video/x-raw",
                        "format", G_TYPE_STRING, "YUY2",//"RGB16",
                        "width", G_TYPE_INT, 480,//384,
                        "height", G_TYPE_INT, 320,//288,
                        "framerate", GST_TYPE_FRACTION, 2, 1,
                        NULL), NULL);

    // gst_bin_add_many(GST_BIN (pipeline), appsrc, conv, videosink, NULL);
    // gst_element_link_many(appsrc, conv, videosink, NULL);

    /* setup appsrc */
    g_object_set(G_OBJECT(appsrc), "stream-type", 0, "format", GST_FORMAT_TIME, NULL);
    g_signal_connect(appsrc, "need-data", G_CALLBACK(cb_need_data), NULL);

    /* play */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_main_loop_run(loop);

    /* clean up */
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT (pipeline));
    g_main_loop_unref(loop);

    return 0;
}