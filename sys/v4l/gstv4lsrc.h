/* Gnome-Streamer
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


#ifndef __GST_V4LSRC_H__
#define __GST_V4LSRC_H__


#include <config.h>
#include <gst/gst.h>

#include <linux/videodev.h>

#include "grab.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_V4LSRC \
  (gst_v4lsrc_get_type())
#define GST_V4LSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4LSRC,GstV4lSrc))
#define GST_V4LSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4LSRC,GstV4lSrcClass))
#define GST_IS_V4LSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4LSRC))
#define GST_IS_V4LSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4LSRC))

// NOTE: per-element flags start with 16 for now
typedef enum {
  GST_V4LSRC_OPEN            = GST_ELEMENT_FLAG_LAST,

  GST_V4LSRC_FLAG_LAST       = GST_ELEMENT_FLAG_LAST+2,
} GstV4lSrcFlags;

typedef struct _GstV4lSrc GstV4lSrc;
typedef struct _GstV4lSrcClass GstV4lSrcClass;

struct _GstV4lSrc {
  GstElement element;

  /* pads */
  GstPad *srcpad;

  /* video device */
  struct GRABBER *grabber;
  gboolean init;

  gint width;
  gint height;
  guint16 format;
  guint32 buffer_size;
  gulong tune;
  gboolean tuned;
  gint input;
  gint norm;
  gint volume;
  gboolean mute;
  gint audio_mode;
  gint color;
  gint bright;
  gint hue;
  gint contrast;
  gchar *device;
};

struct _GstV4lSrcClass {
  GstElementClass parent_class;
};

GType gst_v4lsrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_V4LSRC_H__ */
