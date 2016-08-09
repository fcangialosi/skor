#include <gst/gst.h>
#include <glib.h>

#include "args.h"
#include "common.h"
#include "tunnel_start_server.h"

GST_DEBUG_CATEGORY(gst_skor_tunnel_debug);

struct app_state *g_state;

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
	(void)bus;

	GMainLoop *loop = (GMainLoop *)data;

	switch (GST_MESSAGE_TYPE(msg)) {

		case GST_MESSAGE_EOS:
			g_main_loop_quit(loop);
			break;

		case GST_MESSAGE_ERROR: {
			gchar *debug;
			GError *error;

			gst_message_parse_error(msg, &error, &debug);
			g_free(debug);

			g_printerr("Error: %s\n", error->message);
			g_error_free(error);

			g_main_loop_quit(loop);
			break;
		}
		default:
			break;
	}

	return TRUE;
}

int main(int argc, char *argv[]) {

	gst_init (&argc, &argv);

	struct arguments args;
	if (parse_arguments(argc, argv, &args) < 0)
		return -1;

	// Initialize glib main event loop
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);

	// Create our own debug category
	GST_DEBUG_CATEGORY_INIT(gst_skor_tunnel_debug, "skortunnel", 0, "Skor Tunnel");

	// Set up the outgoing pipeline (temporarily going to ximagesink for testing)
	char pipedesc[1000];
	//snprintf(pipedesc, sizeof(pipedesc),
	//		"skorsrc name=skorsrc ! video/x-raw,format=YUY2,width=65,height=65,framerate=%u/1 ! v4l2sink device=%s",
	//		args.send_framerate, args.output_device);
	snprintf(pipedesc, sizeof(pipedesc),
			"skorsrc name=skorsrc ! video/x-raw,format=YUY2,width=65,height=65,framerate=%u/1 ! ximagesink",
			args.send_framerate);

	// Launch the outgoing pipeline
	GError *error = NULL;
	GstElement *outpipe = gst_parse_launch(pipedesc, &error);
	if (error != NULL) {
		g_printerr("Error creating outgoing GStreamer pipeline:%s.\n", error->message);
		return -1;
	}

	// Get gstreamer bus and attach bus_call handler to handle
	// EOS and error events properly
	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(outpipe));
	guint out_watch_id = gst_bus_add_watch(bus, bus_call, loop);
	gst_object_unref(bus);

	// Get queue (used for passing QR codes from here to skorsrc) and
	GstElement *skorsrc = gst_bin_get_by_name(GST_BIN(outpipe), "skorsrc");
	GAsyncQueue *out_queue;
	g_object_get(skorsrc, "queue", &out_queue, NULL);

	// Set up the incoming pipeline
	// snprintf(pipedesc, sizeof(pipedesc),
	// 	"ximagesrc use-damage=false startx=%d starty=%d endx=%d endy=%d ! videoconvert ! video/x-raw,framerate=%d/1 ! skorsink name=skorsink ! videoconvert ! xvimagesink",
	// 	args.x, args.y, args.x + args.w - 1, args.y + args.h - 1, args.recv_framerate);

	// GstElement *inpipe = gst_parse_launch(pipedesc, &error);
	// if (error != NULL) {
	// 	g_printerr("Error creating outgoing GStreamer pipeline:%s.\n", error->message);
	// 	return -1;
	// }

	//bus = gst_pipeline_get_bus(GST_PIPELINE(inpipe));
	//guint in_watch_id = gst_bus_add_watch(bus, bus_call, loop);
	//gst_object_unref(bus);

	//GstElement *skorsink = gst_bin_get_by_name(GST_BIN(inpipe), "skorsink");
	//g_object_set(skorsink, "dataconsumer", untunnel_packet, NULL);

	// Start thread
	args.out_queue = out_queue;
	GThread *server_thread;
	server_thread = g_thread_new("tunnelinput", (GThreadFunc)tunnel_start_server_loop, &args);

	// Set the pipelines to "playing" state
	gst_element_set_state(outpipe, GST_STATE_PLAYING);

	// Iterate
	g_main_loop_run(loop);

	// Out of the main loop, clean up nicely
	g_thread_unref(server_thread);
	gst_element_set_state (outpipe, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(outpipe));
	g_source_remove(out_watch_id);
	g_main_loop_unref(loop);

	return 0;
}
