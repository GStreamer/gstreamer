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


#ifndef __GST_CACASINK_H__
#define __GST_CACASINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include <caca.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_CACA_DEFAULT_SCREEN_WIDTH 80
#define GST_CACA_DEFAULT_SCREEN_HEIGHT 25
#define GST_CACA_DEFAULT_BPP 24
#define GST_CACA_DEFAULT_RED_MASK GST_VIDEO_BYTE1_MASK_32_INT
#define GST_CACA_DEFAULT_GREEN_MASK GST_VIDEO_BYTE2_MASK_32_INT
#define GST_CACA_DEFAULT_BLUE_MASK GST_VIDEO_BYTE3_MASK_32_INT

//#define GST_CACA_DEFAULT_RED_MASK R_MASK_32_REVERSE_INT
//#define GST_CACA_DEFAULT_GREEN_MASK G_MASK_32_REVERSE_INT
//#define GST_CACA_DEFAULT_BLUE_MASK B_MASK_32_REVERSE_INT

#define GST_TYPE_CACASINK \
  (gst_cacasink_get_type())
#define GST_CACASINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CACASINK,GstCACASink))
#define GST_CACASINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CACASINK,GstCACASink))
#define GST_IS_CACASINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CACASINK))
#define GST_IS_CACASINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CACASINK))

typedef enum {
  GST_CACASINK_OPEN              = GST_ELEMENT_FLAG_LAST,

  GST_CACASINK_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
} GstCACASinkFlags;

typedef struct _GstCACASink GstCACASink;
typedef struct _GstCACASinkClass GstCACASinkClass;

struct _GstCACASink {
  GstVideoSink videosink;

  GstPad *sinkpad;

  gulong format;
  gint screen_width, screen_height;
  guint bpp;
  guint dither;
  guint red_mask, green_mask, blue_mask;

  gint64 correction;
  GstClockID id;

  struct caca_bitmap *bitmap;
};

struct _GstCACASinkClass {
  GstVideoSinkClass parent_class;

  /* signals */
};

GType gst_cacasink_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_CACASINKE_H__ */
