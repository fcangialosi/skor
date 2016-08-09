#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstskorsrc.h"
#include "skorsrc.h"

#include <gst/math-compat.h>

#include <qrencode.h>

#include <string.h>
#include <stdlib.h>

#define TO_16(x) (((x)<<8) | (x))

enum
{
  COLOR_WHITE = 0,
  COLOR_YELLOW,
  COLOR_CYAN,
  COLOR_GREEN,
  COLOR_MAGENTA,
  COLOR_RED,
  COLOR_BLUE,
  COLOR_BLACK,
  COLOR_NEG_I,
  COLOR_POS_Q,
  COLOR_SUPER_BLACK,
  COLOR_DARK_GREY
};

static const struct vts_color_struct vts_colors_bt709_ycbcr_100[] = {
  {235, 128, 128, 255, 255, 255, 255, (235 << 8)},
  {219, 16, 138, 255, 255, 255, 0, (219 << 8)},
  {188, 154, 16, 255, 0, 255, 255, (188 < 8)},
  {173, 42, 26, 255, 0, 255, 0, (173 << 8)},
  {78, 214, 230, 255, 255, 0, 255, (78 << 8)},
  {63, 102, 240, 255, 255, 0, 0, (64 << 8)},
  {32, 240, 118, 255, 0, 0, 255, (32 << 8)},
  {16, 128, 128, 255, 0, 0, 0, (16 << 8)},
  {16, 198, 21, 255, 0, 0, 128, (16 << 8)},     /* -I ? */
  {16, 235, 198, 255, 0, 128, 255, (16 << 8)},  /* +Q ? */
  {0, 128, 128, 255, 0, 0, 0, 0},
  {32, 128, 128, 255, 19, 19, 19, (32 << 8)},
};

static const struct vts_color_struct vts_colors_bt601_ycbcr_100[] = {
  {235, 128, 128, 255, 255, 255, 255, (235 << 8)},
  {210, 16, 146, 255, 255, 255, 0, (219 << 8)},
  {170, 166, 16, 255, 0, 255, 255, (188 < 8)},
  {145, 54, 34, 255, 0, 255, 0, (173 << 8)},
  {106, 202, 222, 255, 255, 0, 255, (78 << 8)},
  {81, 90, 240, 255, 255, 0, 0, (64 << 8)},
  {41, 240, 110, 255, 0, 0, 255, (32 << 8)},
  {16, 128, 128, 255, 0, 0, 0, (16 << 8)},
  {16, 198, 21, 255, 0, 0, 128, (16 << 8)},     /* -I ? */
  {16, 235, 198, 255, 0, 128, 255, (16 << 8)},  /* +Q ? */
  {-0, 128, 128, 255, 0, 0, 0, 0},
  {32, 128, 128, 255, 19, 19, 19, (32 << 8)},
};


static void paint_tmpline_ARGB (paintinfo * p, int x, int w);
static void paint_tmpline_AYUV (paintinfo * p, int x, int w);

static void convert_hline_generic (paintinfo * p, GstVideoFrame * frame, int y);

#define SCALEBITS 10
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1<<SCALEBITS) + 0.5))

#define RGB_TO_Y(r, g, b) \
((FIX(0.29900) * (r) + FIX(0.58700) * (g) + \
  FIX(0.11400) * (b) + ONE_HALF) >> SCALEBITS)

#define RGB_TO_U(r1, g1, b1, shift)\
(((- FIX(0.16874) * r1 - FIX(0.33126) * g1 +         \
     FIX(0.50000) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_V(r1, g1, b1, shift)\
(((FIX(0.50000) * r1 - FIX(0.41869) * g1 -           \
   FIX(0.08131) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_Y_CCIR(r, g, b) \
((FIX(0.29900*219.0/255.0) * (r) + FIX(0.58700*219.0/255.0) * (g) + \
  FIX(0.11400*219.0/255.0) * (b) + (ONE_HALF + (16 << SCALEBITS))) >> SCALEBITS)

#define RGB_TO_U_CCIR(r1, g1, b1, shift)\
(((- FIX(0.16874*224.0/255.0) * r1 - FIX(0.33126*224.0/255.0) * g1 +         \
     FIX(0.50000*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_V_CCIR(r1, g1, b1, shift)\
(((FIX(0.50000*224.0/255.0) * r1 - FIX(0.41869*224.0/255.0) * g1 -           \
   FIX(0.08131*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_Y_CCIR_709(r, g, b) \
((FIX(0.212600*219.0/255.0) * (r) + FIX(0.715200*219.0/255.0) * (g) + \
  FIX(0.072200*219.0/255.0) * (b) + (ONE_HALF + (16 << SCALEBITS))) >> SCALEBITS)

#define RGB_TO_U_CCIR_709(r1, g1, b1, shift)\
(((- FIX(0.114572*224.0/255.0) * r1 - FIX(0.385427*224.0/255.0) * g1 +         \
     FIX(0.50000*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_V_CCIR_709(r1, g1, b1, shift)\
(((FIX(0.50000*224.0/255.0) * r1 - FIX(0.454153*224.0/255.0) * g1 -           \
   FIX(0.045847*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

static void
skorsrc_setup_paintinfo (GstSkorSrc * v, paintinfo * p, int w, int h)
{
  gint a, r, g, b;
  GstVideoInfo *info = &v->info;

  if (info->colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT601) {
    p->colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->colors = vts_colors_bt709_ycbcr_100;
  }

  p->convert_tmpline = convert_hline_generic;
  if (GST_VIDEO_INFO_IS_RGB (info)) {
    p->paint_tmpline = paint_tmpline_ARGB;
  } else {
    p->paint_tmpline = paint_tmpline_AYUV;
  }

  p->tmpline = v->tmpline;
  p->tmpline2 = v->tmpline2;
  p->tmpline_u8 = v->tmpline_u8;
  p->tmpline_u16 = v->tmpline_u16;
  p->n_lines = v->n_lines;
  p->offset = v->offset;
  p->lines = v->lines;

  // a = (v->foreground_color >> 24) & 0xff;
  // r = (v->foreground_color >> 16) & 0xff;
  // g = (v->foreground_color >> 8) & 0xff;
  // b = (v->foreground_color >> 0) & 0xff;
  a = 0xff;
  r = 0xff;
  g = 0xff;
  b = 0xff;
  p->foreground_color.A = a;
  p->foreground_color.R = r;
  p->foreground_color.G = g;
  p->foreground_color.B = b;

  if (info->colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT601) {
    p->foreground_color.Y = RGB_TO_Y_CCIR (r, g, b);
    p->foreground_color.U = RGB_TO_U_CCIR (r, g, b, 0);
    p->foreground_color.V = RGB_TO_V_CCIR (r, g, b, 0);
  } else {
    p->foreground_color.Y = RGB_TO_Y_CCIR_709 (r, g, b);
    p->foreground_color.U = RGB_TO_U_CCIR_709 (r, g, b, 0);
    p->foreground_color.V = RGB_TO_V_CCIR_709 (r, g, b, 0);
  }
  p->foreground_color.gray = RGB_TO_Y (r, g, b);

  // a = (v->background_color >> 24) & 0xff;
  // r = (v->background_color >> 16) & 0xff;
  // g = (v->background_color >> 8) & 0xff;
  // b = (v->background_color >> 0) & 0xff;
  a = 0x00;
  r = 0x00;
  g = 0x00;
  b = 0x00;
  p->background_color.A = a;
  p->background_color.R = r;
  p->background_color.G = g;
  p->background_color.B = b;

  if (info->colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT601) {
    p->background_color.Y = RGB_TO_Y_CCIR (r, g, b);
    p->background_color.U = RGB_TO_U_CCIR (r, g, b, 0);
    p->background_color.V = RGB_TO_V_CCIR (r, g, b, 0);
  } else {
    p->background_color.Y = RGB_TO_Y_CCIR_709 (r, g, b);
    p->background_color.U = RGB_TO_U_CCIR_709 (r, g, b, 0);
    p->background_color.V = RGB_TO_V_CCIR_709 (r, g, b, 0);
  }
  p->background_color.gray = RGB_TO_Y (r, g, b);

  p->subsample = v->subsample;
}

static void
skorsrc_convert_tmpline (paintinfo * p, GstVideoFrame * frame, int j)
{
  int x = p->x_offset;
  int i;
  int width = frame->info.width;
  int height = frame->info.height;
  int n_lines = p->n_lines;
  int offset = p->offset;

  if (x != 0) {
    memcpy (p->tmpline2, p->tmpline, width * 4);
    memcpy (p->tmpline, p->tmpline2 + x * 4, (width - x) * 4);
    memcpy (p->tmpline + (width - x) * 4, p->tmpline2, x * 4);
  }

  for (i = width; i < width + 5; i++) {
    p->tmpline[4 * i + 0] = p->tmpline[4 * (width - 1) + 0];
    p->tmpline[4 * i + 1] = p->tmpline[4 * (width - 1) + 1];
    p->tmpline[4 * i + 2] = p->tmpline[4 * (width - 1) + 2];
    p->tmpline[4 * i + 3] = p->tmpline[4 * (width - 1) + 3];
  }

  p->convert_tmpline (p, frame, j);

  if (j == height - 1) {
    while (j % n_lines - offset != n_lines - 1) {
      j++;
      p->convert_tmpline (p, frame, j);
    }
  }
}

static void
paint_tmpline_ARGB (paintinfo * p, int x, int w)
{
  int offset;
  guint32 value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  value = (p->color->A << 0) | (p->color->R << 8) |
      (p->color->G << 16) | (p->color->B << 24);
#else
  value = (p->color->A << 24) | (p->color->R << 16) |
      (p->color->G << 8) | (p->color->B << 0);
#endif

  // offset = (x * 4);
  offset = x;

  //video_test_src_orc_splat_u32 (p->tmpline + offset, value, w);
  guint32 *ptr = (guint32 *)p->tmpline + offset;
  int i;  
  for (i = 0; i < w; i++) {
    ptr[i] = value;
  }  
}

static void
paint_tmpline_AYUV (paintinfo * p, int x, int w)
{
  int offset;
  guint32 value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  value = (p->color->A << 0) | (p->color->Y << 8) |
      (p->color->U << 16) | ((guint32) p->color->V << 24);
#else
  value = ((guint32) p->color->A << 24) | (p->color->Y << 16) |
      (p->color->U << 8) | (p->color->V << 0);
#endif

  // offset = (x * 4);
  offset = x;

  //video_test_src_orc_splat_u32 (p->tmpline + offset, value, w);
  guint32 *ptr = (guint32 *)p->tmpline + offset;
  int i;  
  for (i = 0; i < w; i++) {
    ptr[i] = value;
  }
}

static void
convert_hline_generic (paintinfo * p, GstVideoFrame * frame, int y)
{
  const GstVideoFormatInfo *finfo, *uinfo;
  gint line, offset, i, width, height, bits;
  guint n_lines;
  gpointer dest;

  finfo = frame->info.finfo;
  uinfo = gst_video_format_get_info (finfo->unpack_format);

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  bits = GST_VIDEO_FORMAT_INFO_DEPTH (uinfo, 0);

  n_lines = p->n_lines;
  offset = p->offset;
  line = y % n_lines;
  dest = p->lines[line];

  if (bits == 16) {
    /* 16 bits */
    for (i = 0; i < width; i++) {
      p->tmpline_u16[i * 4 + 0] = TO_16 (p->tmpline[i * 4 + 0]);
      p->tmpline_u16[i * 4 + 1] = TO_16 (p->tmpline[i * 4 + 1]);
      p->tmpline_u16[i * 4 + 2] = TO_16 (p->tmpline[i * 4 + 2]);
      p->tmpline_u16[i * 4 + 3] = TO_16 (p->tmpline[i * 4 + 3]);
    }
    memcpy (dest, p->tmpline_u16, width * 8);
  } else {
    memcpy (dest, p->tmpline, width * 4);
  }

  if (line - offset == n_lines - 1) {
    gpointer lines[8];
    guint idx;

    y -= n_lines - 1;

    for (i = 0; i < n_lines; i++) {
      idx = CLAMP (y + i + offset, 0, height - 1);
      lines[i] = p->lines[idx % n_lines];
    }

    if (p->subsample)
      gst_video_chroma_resample (p->subsample, lines, width);

    for (i = 0; i < n_lines; i++) {
      idx = y + i + offset;
      if (idx > height - 1)
        break;
      finfo->pack_func (finfo, GST_VIDEO_PACK_FLAG_NONE,
          lines[i], 0, frame->data, frame->info.stride,
          frame->info.chroma_site, idx, width);
    }
  }
}

void
make_image (GstSkorSrc * v, GstVideoFrame * frame)
{
  int x, y, i;
  paintinfo pi = PAINT_INFO_INIT;
  paintinfo *p = &pi;
  int w = frame->info.width, h = frame->info.height;

  skorsrc_setup_paintinfo (v, p, w, h);

  p->color = p->colors + COLOR_GREEN;
  for (i = 0; i < h; i++) {
    p->paint_tmpline (p, 0, w);
    skorsrc_convert_tmpline (p, frame, i);
  }

  QRcode *code;
  if (v->queue == NULL || (code = g_async_queue_try_pop(v->queue)) == NULL)
    return;

  int dotw = MIN(w, h) / code->width, symw = dotw * code->width;
  int offy = (h - symw) / 2, offx = (w - symw) / 2;

  // code
  for (y = 0; y < code->width; y++) {
    for (x = 0; x < code->width; x++) {
      guint dot = code->data[y * code->width + x];

      if ((dot & 0x01) == 0) {
        p->color = p->colors + COLOR_WHITE;
      } else {
        p->color = p->colors + COLOR_BLACK;
      }

      p->paint_tmpline (p, offx + x * dotw, dotw);
    }

    for (i = 0; i < dotw; i++)
      skorsrc_convert_tmpline (p, frame, offy + y * dotw + i);
  }

  QRcode_free(code);
}
