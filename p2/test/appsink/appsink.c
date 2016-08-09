#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdlib.h>
#include <zbar.h>

static gboolean
bus_callback (GstBus *bus, GstMessage *message, gpointer data) 
{
  g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR: {
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);    
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      break;
    default:
      /* unhandled message */
      break;
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *sink;
  gint width, height;
  GstSample *sample;
  gchar *descr;
  GError *error = NULL;
  gint64 duration, position;
  GstStateChangeReturn ret;
  gboolean res;
  GstMapInfo map;

  gst_init (&argc, &argv);

  /* create a new pipeline */
  // pipeline = gst_parse_launch ("ximagesrc use-damage=false ! videoconvert ! videoscale ! "
  //     " appsink name=sink caps=\"video/x-raw,format=RGB,width=160", &error);

  pipeline = gst_parse_launch ("ximagesrc use-damage=false startx=0 starty=0 endx=100 endy=100 ! videoconvert ! videoscale ! "
      " appsink name=sink caps=\"video/x-raw,format=RGB,width=[1,2000],framerate=1/1\"", &error);


  if (error != NULL) {
    g_print ("could not construct pipeline: %s\n", error->message);
    g_error_free (error);
    exit (-1);
  }

  // Monitor bus
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_callback, NULL);
  gst_object_unref (bus);

  /* get sink */
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

  gst_app_sink_set_drop(GST_APP_SINK (sink), TRUE);
  // gst_app_sink_set_max_buffers((GstAppSink*)sink, 1);

  // Start playing
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  int count = 0;
  while (TRUE) {
    GstSample *sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));

    /* if we have a buffer now, convert it to a pixbuf. It's possible that we
     * don't have a buffer because we went EOS right away or had an error. */
    if (sample) {
      GstBuffer *buffer;
      GstCaps *caps;
      GstStructure *s;

      /* get the snapshot buffer format now. We set the caps on the appsink so
       * that it can only be an rgb buffer. The only thing we have not specified
       * on the caps is the height, which is dependant on the pixel-aspect-ratio
       * of the source material */
      caps = gst_sample_get_caps (sample);
      if (!caps) {
        g_print ("could not get snapshot format\n");
        exit (-1);
      }
      s = gst_caps_get_structure (caps, 0);

      /* we need to get the final caps on the buffer to get the size */
      res = gst_structure_get_int (s, "width", &width);
      res |= gst_structure_get_int (s, "height", &height);
      if (!res) {
        g_print ("could not get snapshot dimension\n");
        exit (-1);
      }

      /* create pixmap from buffer and save, gstreamer video buffers have a stride
       * that is rounded up to the nearest multiple of 4 */
      buffer = gst_sample_get_buffer (sample);
      gst_buffer_map (buffer, &map, GST_MAP_READ);
      GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data (map.data,
          GDK_COLORSPACE_RGB, FALSE, 8, width, height,
          GST_ROUND_UP_4 (width * 3), NULL, NULL);

      /* save the pixbuf */
      gchar *filename = g_strdup_printf ("snapshot%10d.png", count++);
      gdk_pixbuf_save (pixbuf, filename, "png", &error, NULL);
      gst_buffer_unmap (buffer, &map);
      gst_sample_unref (sample);
      g_free (filename);
    } else {
      g_print ("could not make snapshot\n");
    }


  }


  /* cleanup and exit */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  exit (0);
}