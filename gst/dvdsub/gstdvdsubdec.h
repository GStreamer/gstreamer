/* GStreamer
 * Copyright (C) <2005> Jan Schmidt <jan@fluendo.com>
 * Copyright (C) <2002> Wim Taymans <wim@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/gst.h>
#include <gst/video/video.h>

#define GST_TYPE_DVD_SUB_DEC             (gst_dvd_sub_dec_get_type())
#define GST_DVD_SUB_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVD_SUB_DEC,GstDvdSubDec))
#define GST_DVD_SUB_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVD_SUB_DEC,GstDvdSubDecClass))
#define GST_IS_DVD_SUB_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVD_SUB_DEC))
#define GST_IS_DVD_SUB_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVD_SUB_DEC))

typedef struct _GstDvdSubDec GstDvdSubDec;
typedef struct _GstDvdSubDecClass GstDvdSubDecClass;

/* Hold premultimplied colour values */
typedef struct Color_val
{
  guchar Y_R;
  guchar U_G;
  guchar V_B;
  guchar A;

} Color_val;

struct _GstDvdSubDec
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  gint in_width, in_height;

  /* Collect together subtitle buffers until we have a full control sequence */
  GstBuffer *partialbuf;
  GstMapInfo partialmap;
  gboolean have_title;

  guchar subtitle_index[4];
  guchar menu_index[4];
  guchar subtitle_alpha[4];
  guchar menu_alpha[4];

  guint32 current_clut[16];
  Color_val palette_cache_yuv[4];
  Color_val hl_palette_cache_yuv[4];

  Color_val palette_cache_rgb[4];
  Color_val hl_palette_cache_rgb[4];

  GstVideoInfo info;
  gboolean use_ARGB;
  GstClockTime next_ts;

  /*
   * State info for the current subpicture
   * buffer
   */
  guchar *parse_pos;

  guint16 packet_size;
  guint16 data_size;

  gint offset[2];

  gboolean forced_display;
  gboolean visible;

  gint left, top, right, bottom;
  gint hl_left, hl_top, hl_right, hl_bottom;

  gint current_button;

  GstClockTime next_event_ts;

  gboolean buf_dirty;
};

struct _GstDvdSubDecClass
{
  GstElementClass parent_class;
};

GType gst_dvd_sub_dec_get_type (void);
