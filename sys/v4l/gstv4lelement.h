/* G-Streamer generic V4L element - generic V4L calls handling
 * Copyright (C) 2001 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_V4LELEMENT_H__
#define __GST_V4LELEMENT_H__

#include <config.h>
#include <gst/gst.h>
#include <sys/types.h>
#include <linux/videodev.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_V4LELEMENT \
  (gst_v4lelement_get_type())
#define GST_V4LELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4LELEMENT,GstV4lElement))
#define GST_V4LELEMENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4LELEMENT,GstV4lElementClass))
#define GST_IS_V4LELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4LELEMENT))
#define GST_IS_V4LELEMENT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4LELEMENT))

typedef struct _GstV4lElement GstV4lElement;
typedef struct _GstV4lElementClass GstV4lElementClass;

struct _GstV4lElement {
  GstElement element;

  /* the video device */
  char *videodev;

  /* the video-device's file descriptor */
  gint video_fd;

  /* the video buffer (mmap()'ed) */
  guint8 *buffer;

  /* the video-device's capabilities */
  struct video_capability vcap;

  /* caching values */
  gint channel;
  gint norm;
  gulong frequency;
  gboolean mute;
  gint volume;
  gint mode;
  gint brightness;
  gint hue;
  gint contrast;
  gint saturation;
};

struct _GstV4lElementClass {
  GstElementClass parent_class;
};

GType gst_v4lelement_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_V4LELEMENT_H__ */
