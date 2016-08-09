#ifndef __SKOR_SRC_H__
#define __SKOR_SRC_H__

#include <glib.h>

struct vts_color_struct {
  guint8 Y, U, V, A;
  guint8 R, G, B;
  guint16 gray;
};

typedef struct paintinfo_struct paintinfo;

struct paintinfo_struct
{
  const struct vts_color_struct *colors;
  const struct vts_color_struct *color;

  void (*paint_tmpline) (paintinfo * p, int x, int w);
  void (*convert_tmpline) (paintinfo * p, GstVideoFrame *frame, int y);
  void (*convert_hline) (paintinfo * p, GstVideoFrame *frame, int y);
  GstVideoChromaResample *subsample;
  int x_offset;

  int x_invert;
  int y_invert;

  guint8 *tmpline;
  guint8 *tmpline2;
  guint8 *tmpline_u8;
  guint16 *tmpline_u16;

  guint n_lines;
  gint offset;
  gpointer *lines;

  struct vts_color_struct foreground_color;
  struct vts_color_struct background_color;
};

#define PAINT_INFO_INIT {0, }

void make_image (GstSkorSrc * v, GstVideoFrame *frame);

#endif
