/* G-Streamer generic V4L element - generic V4L calls handling
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <gst/gst.h>
#include <gst/xwindowlistener/xwindowlistener.h>

/* Because of some really cool feature in video4linux1, also known as
 * 'not including sys/types.h and sys/time.h', we had to include it
 * ourselves. In all their intelligence, these people decided to fix
 * this in the next version (video4linux2) in such a cool way that it
 * breaks all compilations of old stuff...
 * The real problem is actually that linux/time.h doesn't use proper
 * macro checks before defining types like struct timeval. The proper
 * fix here is to either fuck the kernel header (which is what we do
 * by defining _LINUX_TIME_H, an innocent little hack) or by fixing it
 * upstream, which I'll consider doing later on. If you get compiler
 * errors here, check your linux/time.h && sys/time.h header setup.
 */
#include <sys/types.h>
#define _LINUX_TIME_H
#include <linux/videodev.h>
#include <gst/gst.h>

G_BEGIN_DECLS

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
#define GST_V4LELEMENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_V4LELEMENT, GstV4lElementClass))

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

  /* the video device's capabilities */
  struct video_capability vcap;

  /* the video device's window properties */
  struct video_window vwin;

  /* some more info about the current input's capabilities */
  struct video_channel vchan;

  /* lists... */
  GList *colors;
  GList *norms;
  GList *channels;

  /* X-overlay */
  GstXWindowListener *overlay;
  XID xwindow_id;

  /* caching values */
  gchar *display;
};

struct _GstV4lElementClass {
  GstElementClass parent_class;

  /* probed devices */
  GList *devices;

  /* signals */
  void     (*open)           (GstElement  *element,
                              const gchar *device);
  void     (*close)          (GstElement  *element,
                              const gchar *device);

  /* actions */
  gboolean (*get_attribute)   (GstElement  *element,
                               const gchar *attr_name,
                               int         *value);
  gboolean (*set_attribute)   (GstElement  *element,
                               const gchar *attr_name,
                               const int    value);
};

GType gst_v4lelement_get_type(void);


G_END_DECLS

#endif /* __GST_V4LELEMENT_H__ */
