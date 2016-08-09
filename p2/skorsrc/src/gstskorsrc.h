#ifndef __GST_SKORSRC_H__
#define __GST_SKORSRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

GST_DEBUG_CATEGORY_EXTERN (gst_skor_src_debug);

G_BEGIN_DECLS

#define GST_TYPE_SKORSRC \
  (gst_skor_src_get_type())
#define GST_SKORSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SKORSRC,GstSkorSrc))
#define GST_SKORSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SKORSRC,GstSkorSrcClass))
#define GST_IS_SKORSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SKORSRC))
#define GST_IS_SKORSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SKORSRC))

typedef struct _GstSkorSrc      GstSkorSrc;
typedef struct _GstSkorSrcClass GstSkorSrcClass;

struct _GstSkorSrc
{
  GstPushSrc element;

  GAsyncQueue *queue;

  /* video state */
  GstVideoInfo info;
  GstVideoChromaResample *subsample;

  /* running time and frames for current caps */
  GstClockTime running_time;            /* total running time */
  gint64 n_frames;                      /* total frames sent */
  gboolean reverse;

  /* previous caps running time and frames */
  GstClockTime accum_rtime;              /* accumulated running_time */
  gint64 accum_frames;                  /* accumulated frames */

  /* temporary AYUV/ARGB scanline */
  guint8 *tmpline_u8;
  guint8 *tmpline;
  guint8 *tmpline2;
  guint16 *tmpline_u16;

  guint n_lines;
  gint offset;
  gpointer *lines;
};

struct _GstSkorSrcClass 
{
  GstPushSrcClass parent_class;
};

GType gst_skor_src_get_type (void);

G_END_DECLS

#endif /* __GST_SKORSRC_H__ */
