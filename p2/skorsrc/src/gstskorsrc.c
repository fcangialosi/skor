/**
 * GStreamer SkorSrc Element
 *
 * Converts QRcode objects (from libqrencode) into raw video frames
 * that can be displayed using a sink, such as a v4l2 virtual webcam.
 *
 * Original version by: Phil Kim <pyk@cs.umd.edu>
 * Modified by: Frank Cangialosi <frank@cs.umd.edu>
 * Last modified: August 2016
 *
 * Sanity check:
 * gst-launch-1.0 -v -m fakesrc ! skorsrc ! fakesink silent=TRUE
 *
 * Should be run *indirectly* by executing the tunnel program, which constructs
 * a pipeline such as the following and attains a handle to the QR code queue:
 * skorsrc ! video/x-raw,format=YUY2,framerate=1/1 ! v4l2sink device=/dev/video0
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "gstskorsrc.h"
#include "skorsrc.h"

#include <string.h>
#include <stdlib.h>

GST_DEBUG_CATEGORY (gst_skor_src_debug);
#define GST_CAT_DEFAULT gst_skor_src_debug

enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_INPUTQUEUE
};

static GstStaticPadTemplate gst_skor_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL))
    );

#define gst_skor_src_parent_class parent_class
G_DEFINE_TYPE (GstSkorSrc, gst_skor_src, GST_TYPE_PUSH_SRC);

static void gst_skor_src_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_skor_src_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_skor_src_finalize (GObject * object);

static gboolean gst_skor_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static GstCaps *gst_skor_src_src_fixate (GstBaseSrc * bsrc, GstCaps * caps);

static gboolean gst_skor_src_is_seekable (GstBaseSrc * psrc);
static gboolean gst_skor_src_query (GstBaseSrc * bsrc, GstQuery * query);

static gboolean gst_skor_src_decide_allocation (GstBaseSrc * bsrc,GstQuery * query);
static GstFlowReturn gst_skor_src_fill (GstPushSrc * psrc, GstBuffer * buffer);
static gboolean gst_skor_src_start (GstBaseSrc * basesrc);
static gboolean gst_skor_src_stop (GstBaseSrc * basesrc);

/* Class initializer */
static void
gst_skor_src_class_init (GstSkorSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_skor_src_set_property;
  gobject_class->get_property = gst_skor_src_get_property;
  gobject_class->finalize = gst_skor_src_finalize;

  g_object_class_install_property (gobject_class, PROP_INPUTQUEUE,
      g_param_spec_pointer ("queue", "Queue", "Input queue from which the plugin will consume QR codes", G_PARAM_READABLE));

  gst_element_class_set_static_metadata (gstelement_class,
      "Skor QR code source", "Source/Video",
      "Converts QRcode objects (from libqrencode) into raw video frames.", "Frank Cangialosi <frank@cs.umd.edu>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_skor_src_template));

  gstbasesrc_class->set_caps = gst_skor_src_setcaps;
  gstbasesrc_class->fixate = gst_skor_src_src_fixate;
  gstbasesrc_class->is_seekable = gst_skor_src_is_seekable;
  gstbasesrc_class->query = gst_skor_src_query;
  gstbasesrc_class->start = gst_skor_src_start;
  gstbasesrc_class->stop = gst_skor_src_stop;
  gstbasesrc_class->decide_allocation = gst_skor_src_decide_allocation;

  gstpushsrc_class->fill = gst_skor_src_fill;
}

/* Instance initializer */
static void
gst_skor_src_init (GstSkorSrc * src)
{
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);

  src->queue = g_async_queue_new ();
}

static void
gst_skor_src_finalize (GObject * object)
{
  GstSkorSrc *src = GST_SKORSRC (object);

  g_async_queue_unref (src->queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_skor_src_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstStructure *structure;

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);

  if (gst_structure_has_field (structure, "pixel-aspect-ratio"))
    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", 1, 1);
  else
    gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
        NULL);

  if (gst_structure_has_field (structure, "colorimetry"))
    gst_structure_fixate_field_string (structure, "colorimetry", "bt601");
  if (gst_structure_has_field (structure, "chroma-site"))
    gst_structure_fixate_field_string (structure, "chroma-site", "mpeg2");

  if (gst_structure_has_field (structure, "interlace-mode"))
    gst_structure_fixate_field_string (structure, "interlace-mode",
        "progressive");
  else
    gst_structure_set (structure, "interlace-mode", G_TYPE_STRING,
        "progressive", NULL);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);

  return caps;
}

static void
gst_skor_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_skor_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSkorSrc *src = GST_SKORSRC (object);

  switch (prop_id) {
    case PROP_INPUTQUEUE:
      g_value_set_pointer (value, src->queue);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_skor_src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstSkorSrc *skorsrc;
  GstBufferPool *pool;
  gboolean update;
  guint size, min, max;
  GstStructure *config;
  GstCaps *caps = NULL;

  skorsrc = GST_SKORSRC (bsrc);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    /* adjust size */
    size = MAX (size, skorsrc->info.size);
    update = TRUE;
  } else {
    pool = NULL;
    size = skorsrc->info.size;
    min = max = 0;
    update = FALSE;
  }

  /* no downstream pool, make our own */
  if (pool == NULL) {
      pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);

  gst_query_parse_allocation (query, &caps, NULL);
  if (caps)
    gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);

  if (update)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (pool)
    gst_object_unref (pool);

  return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
}

static gboolean
gst_skor_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  const GstStructure *structure;
  GstSkorSrc *skorsrc;
  GstVideoInfo info;
  guint i;
  guint n_lines;
  gint offset;

  skorsrc = GST_SKORSRC (bsrc);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    /* we can use the parsing code */
    if (!gst_video_info_from_caps (&info, caps))
      goto parse_failed;

  } else {
    goto unsupported_caps;
  }

  /* create chroma subsampler */
  if (skorsrc->subsample)
    gst_video_chroma_resample_free (skorsrc->subsample);
    skorsrc->subsample = gst_video_chroma_resample_new (0,
      info.chroma_site, 0, info.finfo->unpack_format, -info.finfo->w_sub[2],
      -info.finfo->h_sub[2]);

  for (i = 0; i < skorsrc->n_lines; i++)
    g_free (skorsrc->lines[i]);
  g_free (skorsrc->lines);

  if (skorsrc->subsample != NULL) {
    gst_video_chroma_resample_get_info (skorsrc->subsample, &n_lines, &offset);
  } else {
    n_lines = 1;
    offset = 0;
  }

  skorsrc->lines = g_malloc (sizeof (gpointer) * n_lines);
  for (i = 0; i < n_lines; i++)
    skorsrc->lines[i] = g_malloc ((info.width + 16) * 8);
  skorsrc->n_lines = n_lines;
  skorsrc->offset = offset;

  /* looks ok here */
  skorsrc->info = info;

  GST_DEBUG_OBJECT (skorsrc, "size %dx%d, %d/%d fps",
      info.width, info.height, info.fps_n, info.fps_d);

  g_free (skorsrc->tmpline);
  g_free (skorsrc->tmpline2);
  g_free (skorsrc->tmpline_u8);
  g_free (skorsrc->tmpline_u16);
  skorsrc->tmpline_u8 = g_malloc (info.width + 8);
  skorsrc->tmpline = g_malloc ((info.width + 8) * 4);
  skorsrc->tmpline2 = g_malloc ((info.width + 8) * 4);
  skorsrc->tmpline_u16 = g_malloc ((info.width + 16) * 8);

  skorsrc->accum_rtime += skorsrc->running_time;
  skorsrc->accum_frames += skorsrc->n_frames;

  skorsrc->running_time = 0;
  skorsrc->n_frames = 0;

  return TRUE;

  /* ERRORS */
parse_failed:
  {
    GST_DEBUG_OBJECT (bsrc, "failed to parse caps");
    return FALSE;
  }
unsupported_caps:
  {
    GST_DEBUG_OBJECT (bsrc, "unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static gboolean
gst_skor_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res = FALSE;
  GstSkorSrc *src;

  src = GST_SKORSRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_video_info_convert (&src->info, src_fmt, src_val, dest_fmt,
          &dest_val);
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      if (src->info.fps_n > 0) {
        GstClockTime latency;

        latency =
            gst_util_uint64_scale (GST_SECOND, src->info.fps_d,
            src->info.fps_n);
        gst_query_set_latency (query,
            gst_base_src_is_live (GST_BASE_SRC_CAST (src)), latency,
            GST_CLOCK_TIME_NONE);
        GST_DEBUG_OBJECT (src, "Reporting latency of %" GST_TIME_FORMAT,
            GST_TIME_ARGS (latency));
        res = TRUE;
      }
      break;
    }
    case GST_QUERY_DURATION:{
      if (bsrc->num_buffers != -1) {
        GstFormat format;

        gst_query_parse_duration (query, &format, NULL);
        switch (format) {
          case GST_FORMAT_TIME:{
            gint64 dur = gst_util_uint64_scale_int_round (bsrc->num_buffers
                * GST_SECOND, src->info.fps_d, src->info.fps_n);
            res = TRUE;
            gst_query_set_duration (query, GST_FORMAT_TIME, dur);
            goto done;
          }
          case GST_FORMAT_BYTES:
            res = TRUE;
            gst_query_set_duration (query, GST_FORMAT_BYTES,
                bsrc->num_buffers * src->info.size);
            goto done;
          default:
            break;
        }
      }
      /* fall through */
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }
done:
  return res;
}

static gboolean
gst_skor_src_is_seekable (GstBaseSrc * psrc)
{
  return FALSE;
}

static GstFlowReturn
gst_skor_src_fill (GstPushSrc * psrc, GstBuffer * buffer)
{
  GstSkorSrc *src;
  GstClockTime next_time;
  GstVideoFrame frame;
  gconstpointer pal;
  gsize palsize;

  src = GST_SKORSRC (psrc);

  if (G_UNLIKELY (GST_VIDEO_INFO_FORMAT (&src->info) == GST_VIDEO_FORMAT_UNKNOWN))
    goto not_negotiated;

  /* 0 framerate and we are at the second frame, eos */
  if (G_UNLIKELY (src->info.fps_n == 0 && src->n_frames == 1))
    goto eos;

  if (G_UNLIKELY (src->n_frames == -1)) {
    /* EOS for reverse playback */
    goto eos;
  }

  GST_LOG_OBJECT (src, "creating buffer from pool for frame %d", (gint) src->n_frames);

  if (!gst_video_frame_map (&frame, &src->info, buffer, GST_MAP_WRITE))
    goto invalid_frame;

  GST_BUFFER_PTS (buffer) = src->accum_rtime + src->running_time;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;

  gst_object_sync_values (GST_OBJECT (psrc), GST_BUFFER_PTS (buffer));

  make_image(src, &frame);

  if ((pal = gst_video_format_get_palette (GST_VIDEO_FRAME_FORMAT (&frame), &palsize))) {
    memcpy (GST_VIDEO_FRAME_PLANE_DATA (&frame, 1), pal, palsize);
  }

  gst_video_frame_unmap (&frame);

  GST_DEBUG_OBJECT (src, "Timestamp: %" GST_TIME_FORMAT " = accumulated %"
      GST_TIME_FORMAT " + running time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (src->accum_rtime),
      GST_TIME_ARGS (src->running_time));

  GST_BUFFER_OFFSET (buffer) = src->accum_frames + src->n_frames;
  if (src->reverse) {
    src->n_frames--;
  } else {
    src->n_frames++;
  }
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET (buffer) + 1;
  if (src->info.fps_n) {
    next_time = gst_util_uint64_scale_int (src->n_frames * GST_SECOND,
        src->info.fps_d, src->info.fps_n);
    if (src->reverse) {
      GST_BUFFER_DURATION (buffer) = src->running_time - next_time;
    } else {
      GST_BUFFER_DURATION (buffer) = next_time - src->running_time;
    }
  } else {
    next_time = 0;
    /* NONE means forever */
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  }

  src->running_time = next_time;

  return GST_FLOW_OK;

not_negotiated:
  {
    return GST_FLOW_NOT_NEGOTIATED;
  }
eos:
  {
    GST_DEBUG_OBJECT (src, "eos: 0 framerate, frame %d", (gint) src->n_frames);
    return GST_FLOW_EOS;
  }
invalid_frame:
  {
    GST_DEBUG_OBJECT (src, "invalid frame");
    return GST_FLOW_OK;
  }
}

static gboolean
gst_skor_src_start (GstBaseSrc * basesrc)
{
  GstSkorSrc *src = GST_SKORSRC (basesrc);

  src->running_time = 0;
  src->n_frames = 0;
  src->accum_frames = 0;
  src->accum_rtime = 0;

  gst_video_info_init (&src->info);

  return TRUE;
}

static gboolean
gst_skor_src_stop (GstBaseSrc * basesrc)
{
  GstSkorSrc *src = GST_SKORSRC (basesrc);
  guint i;

  g_free (src->tmpline);
  src->tmpline = NULL;
  g_free (src->tmpline2);
  src->tmpline2 = NULL;
  g_free (src->tmpline_u8);
  src->tmpline_u8 = NULL;
  g_free (src->tmpline_u16);
  src->tmpline_u16 = NULL;
  if (src->subsample)
    gst_video_chroma_resample_free (src->subsample);
  src->subsample = NULL;

  for (i = 0; i < src->n_lines; i++)
    g_free (src->lines[i]);
  g_free (src->lines);
  src->n_lines = 0;
  src->lines = NULL;

  return TRUE;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
skorsrc_init (GstPlugin * skorsrc)
{
  GST_DEBUG_CATEGORY_INIT (gst_skor_src_debug, "skorsrc",
      0, "Skor Video Source");

  return gst_element_register (skorsrc, "skorsrc", GST_RANK_NONE,
      GST_TYPE_SKORSRC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstskorsrc"
#endif

/* gstreamer looks for this structure to register skorsrcs
 *
 * exchange the string 'Template skorsrc' with your skorsrc description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    skorsrc,
    "A test video source for Skor",
    skorsrc_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
