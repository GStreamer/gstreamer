/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_MPEG2SUBT_H__
#define __GST_MPEG2SUBT_H__


#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_MPEG2SUBT \
  (gst_mpeg2subt_get_type())
#define GST_MPEG2SUBT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG2SUBT,GstMpeg2Subt))
#define GST_MPEG2SUBT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG2SUBT,GstMpeg2Subt))
#define GST_IS_MPEG2SUBT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG2SUBT))
#define GST_IS_MPEG2SUBT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG2SUBT))

typedef struct _GstMpeg2Subt GstMpeg2Subt;
typedef struct _GstMpeg2SubtClass GstMpeg2SubtClass;

/* Hold premultimplied colour values */
typedef struct YUVA_val {
  guint16 Y;
  guint16 U;
  guint16 V;
  guint16 A;
} YUVA_val;

struct _GstMpeg2Subt {
  GstElement element;

  GstPad *videopad,*subtitlepad,*srcpad;

  GstBuffer *partialbuf;	/* Collect together subtitle buffers until we have a full control sequence */
  GstBuffer *hold_frame;	/* Hold back one frame of video */
  GstBuffer *still_frame;

  guint16 packet_size;
  guint16 data_size;

  gint offset[2];

  YUVA_val palette_cache[4];

  /* 
   * Store 1 line width of U, V and A respectively.
   * Y is composited direct onto the frame.
   */
  guint16 *out_buffers[3];
  guchar subtitle_index[4];
  guchar menu_index[4];
  guchar subtitle_alpha[4];
  guchar menu_alpha[4];

  guint32 current_clut[16];

  gboolean have_title;
  gboolean forced_display;

  GstClockTime start_display_time;
  GstClockTime end_display_time;
  gint left, top, 
      right, bottom;
  gint clip_left, clip_top, 
      clip_right, clip_bottom;

  gint in_width, in_height;
  gint current_button;

  GstData *pending_video_buffer;
  GstClockTime next_video_time;
  GstData *pending_subtitle_buffer;
  GstClockTime next_subtitle_time;
};

struct _GstMpeg2SubtClass {
  GstElementClass parent_class;
};

GType gst_mpeg2subt_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_MPEG2SUBT_H__ */
