/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2015 Phil <<user@hostname.org>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-skorsink
 *
 * FIXME:Describe skorsink here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! skorsink ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <math.h>
#include <string.h>

#include "gstskorsink.h"


GST_DEBUG_CATEGORY_STATIC (gst_skor_sink_debug);
#define GST_CAT_DEFAULT gst_skor_sink_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MESSAGE,
  PROP_ATTACH_FRAME,
  PROP_CACHE,
  PROP_DATA_CONSUMER
};

#define DEFAULT_CACHE        FALSE
#define DEFAULT_MESSAGE      FALSE
#define DEFAULT_ATTACH_FRAME FALSE

#define YUV_CAPS \
    "{ Y800, I420, YV12, NV12, NV21, Y41B, Y42B, YUV9, YVU9 }"

static GstStaticPadTemplate gst_skor_sink_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (YUV_CAPS)));

static GstStaticPadTemplate gst_skor_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (YUV_CAPS)));

#define gst_skor_sink_parent_class parent_class
G_DEFINE_TYPE (GstSkorSink, gst_skor_sink, GST_TYPE_VIDEO_FILTER);

static void gst_skor_sink_finalize (GObject * object);
static void gst_skor_sink_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_skor_sink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_skor_sink_start (GstBaseTransform * base);
static gboolean gst_skor_sink_stop (GstBaseTransform * base);

static GstFlowReturn gst_skor_sink_transform_frame_ip (GstVideoFilter * vfilter, GstVideoFrame * frame);

static void
gst_skor_sink_class_init (GstSkorSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *trans_class;
  GstVideoFilterClass *vfilter_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  vfilter_class = GST_VIDEO_FILTER_CLASS (klass);

  gobject_class->set_property = gst_skor_sink_set_property;
  gobject_class->get_property = gst_skor_sink_get_property;
  gobject_class->finalize = gst_skor_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_MESSAGE,
      g_param_spec_boolean ("message", "message",
          "Post a barcode message for each detected code",
          DEFAULT_MESSAGE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ATTACH_FRAME,
      g_param_spec_boolean ("attach-frame", "Attach frame",
          "Attach a frame dump to each barcode message",
          DEFAULT_ATTACH_FRAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CACHE,
      g_param_spec_boolean ("cache", "cache",
          "Enable or disable the inter-image result cache",
          DEFAULT_CACHE,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DATA_CONSUMER,
      g_param_spec_pointer ("dataconsumer", "Data Consumer",
          "Function pointer of type void (*)(const char *) that will be invoked for every decoded datum",
          G_PARAM_WRITABLE | GST_PARAM_MUTABLE_READY | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple(gstelement_class,
    "SkorSink",
    "Filter/Analyzer/Video",
    "Detect bar codes in the video streams",
    "Phil <pyk@cs.umd.edu>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_skor_sink_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_skor_sink_sink_template));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_skor_sink_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_skor_sink_stop);
  trans_class->transform_ip_on_passthrough = FALSE;

  vfilter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_skor_sink_transform_frame_ip);  
}

static void
gst_skor_sink_init (GstSkorSink * filter)
{
  filter->cache = DEFAULT_CACHE;
  filter->message = DEFAULT_MESSAGE;
  filter->attach_frame = DEFAULT_ATTACH_FRAME;
  filter->data_consumer = NULL;

  filter->scanner = zbar_image_scanner_create ();
}

static void
gst_skor_sink_finalize (GObject * object)
{
  GstSkorSink *sink = GST_SKORSINK (object);

  zbar_image_scanner_destroy (sink->scanner);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_skor_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSkorSink *filter = GST_SKORSINK (object);

  switch (prop_id) {
    case PROP_CACHE:
      filter->cache = g_value_get_boolean (value);
      break;
    case PROP_MESSAGE:
      filter->message = g_value_get_boolean (value);
      break;
    case PROP_ATTACH_FRAME:
      filter->attach_frame = g_value_get_boolean (value);
      break;
    case PROP_DATA_CONSUMER:
      filter->data_consumer = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_skor_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSkorSink *filter = GST_SKORSINK (object);

  switch (prop_id) {
    case PROP_CACHE:
      g_value_set_boolean (value, filter->cache);
      break;
    case PROP_MESSAGE:
      g_value_set_boolean (value, filter->message);
      break;
    case PROP_ATTACH_FRAME:
      g_value_set_boolean (value, filter->attach_frame);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_skor_sink_transform_frame_ip (GstVideoFilter * vfilter, GstVideoFrame * frame)
{
  GstSkorSink *sink = GST_SKORSINK (vfilter);
  gpointer data;
  gint stride, height;
  zbar_image_t *image;
  const zbar_symbol_t *symbol;
  int n;

  image = zbar_image_create ();

  /* all formats we support start with an 8-bit Y plane. zbar doesn't need
   * to know about the chroma plane(s) */
  data = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  zbar_image_set_format (image, GST_MAKE_FOURCC ('Y', '8', '0', '0'));
  zbar_image_set_size (image, stride, height);
  zbar_image_set_data (image, (gpointer) data, stride * height, NULL);

  /* scan the image for barcodes */
  n = zbar_scan_image (sink->scanner, image);
  if (G_UNLIKELY (n == -1)) {
    GST_WARNING_OBJECT (sink, "Error trying to scan frame. Skipping");
    goto out;
  }
  if (n == 0)
    goto out;

  /* extract results */
  symbol = zbar_image_first_symbol (image);
  for (; symbol; symbol = zbar_symbol_next (symbol)) {
    zbar_symbol_type_t typ = zbar_symbol_get_type (symbol);
    const char *data = zbar_symbol_get_data (symbol);
    gint quality = zbar_symbol_get_quality (symbol);

    GST_DEBUG_OBJECT (sink, "decoded %s symbol \"%s\" at quality %d",
        zbar_get_symbol_name (typ), data, quality);

    if (sink->cache && zbar_symbol_get_count (symbol) != 0)
      continue;

    if (sink->data_consumer)
      sink->data_consumer (data);

    if (sink->message) {
      GstMessage *m;
      GstStructure *s;
      GstSample *sample;
      GstCaps *sample_caps;

      s = gst_structure_new ("barcode",
          "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (frame->buffer),
          "type", G_TYPE_STRING, zbar_get_symbol_name (typ),
          "symbol", G_TYPE_STRING, data, "quality", G_TYPE_INT, quality, NULL);

      if (sink->attach_frame) {
        /* create a sample from image */
        sample_caps = gst_video_info_to_caps (&frame->info);
        sample = gst_sample_new (frame->buffer, sample_caps, NULL, NULL);
        gst_caps_unref (sample_caps);
        gst_structure_set (s, "frame", GST_TYPE_SAMPLE, sample, NULL);
        gst_sample_unref (sample);
      }

      m = gst_message_new_element (GST_OBJECT (sink), s);
      gst_element_post_message (GST_ELEMENT (sink), m);

    } else if (sink->attach_frame)
      GST_WARNING_OBJECT (sink,
          "attach-frame=true has no effect if message=false");
  }

out:
  /* clean up */
  zbar_image_scanner_recycle_image (sink->scanner, image);
  zbar_image_destroy (image);

  return GST_FLOW_OK;
}

static gboolean
gst_skor_sink_start (GstBaseTransform * base)
{
  GstSkorSink *sink = GST_SKORSINK (base);

  /* start the cache if enabled (e.g. for filtering dupes) */
  zbar_image_scanner_enable_cache (sink->scanner, sink->cache);

  return TRUE;
}

static gboolean
gst_skor_sink_stop (GstBaseTransform * base)
{
  GstSkorSink *sink = GST_SKORSINK (base);

  /* stop the cache if enabled (e.g. for filtering dupes) */
  zbar_image_scanner_enable_cache (sink->scanner, sink->cache);

  return TRUE;
}

static gboolean
skorsink_init (GstPlugin * skorsink)
{
  GST_DEBUG_CATEGORY_INIT (gst_skor_sink_debug, "skorsink", 0, "skorsink");

  return gst_element_register (skorsink, "skorsink", GST_RANK_NONE, GST_TYPE_SKORSINK);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstskorsink"
#endif

/* gstreamer looks for this structure to register skorsinks
 *
 * exchange the string 'Template skorsink' with your skorsink description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    skorsink,
    "Skor sink",
    skorsink_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
