/* GStreamer Wayland video sink
 *
 * Copyright (C) 2014 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef __GST_WL_BUFFER_H__
#define __GST_WL_BUFFER_H__

#include "wldisplay.h"

G_BEGIN_DECLS

#define GST_TYPE_WL_BUFFER                  (gst_wl_buffer_get_type ())
#define GST_WL_BUFFER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WL_BUFFER, GstWlBuffer))
#define GST_IS_WL_BUFFER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WL_BUFFER))
#define GST_WL_BUFFER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_WL_BUFFER, GstWlBufferClass))
#define GST_IS_WL_BUFFER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_WL_BUFFER))
#define GST_WL_BUFFER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_WL_BUFFER, GstWlBufferClass))

typedef struct _GstWlBuffer GstWlBuffer;
typedef struct _GstWlBufferClass GstWlBufferClass;

struct _GstWlBuffer
{
  GObject parent_instance;

  struct wl_buffer * wlbuffer;
  GstBuffer *gstbuffer;

  GstWlDisplay *display;

  gboolean used_by_compositor;
};

struct _GstWlBufferClass
{
  GObjectClass parent_class;
};

GType gst_wl_buffer_get_type (void);

GstWlBuffer * gst_buffer_add_wl_buffer (GstBuffer * gstbuffer,
    struct wl_buffer * wlbuffer, GstWlDisplay * display);
GstWlBuffer * gst_buffer_get_wl_buffer (GstWlDisplay * display, GstBuffer * gstbuffer);

void gst_wl_buffer_force_release_and_unref (GstBuffer *buf, GstWlBuffer * self);

void gst_wl_buffer_attach (GstWlBuffer * self, struct wl_surface *surface);

G_END_DECLS

#endif /* __GST_WL_BUFFER_H__ */
