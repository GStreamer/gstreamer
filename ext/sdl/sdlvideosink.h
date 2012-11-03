/* GStreamer SDL plugin
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SDLVIDEOSINK_H__
#define __GST_SDLVIDEOSINK_H__

#include <gst/video/gstvideosink.h>

#include <SDL.h>

G_BEGIN_DECLS

#define GST_TYPE_SDLVIDEOSINK \
  (gst_sdlvideosink_get_type())
#define GST_SDLVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SDLVIDEOSINK,GstSDLVideoSink))
#define GST_SDLVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SDLVIDEOSINK,GstSDLVideoSinkClass))
#define GST_IS_SDLVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SDLVIDEOSINK))
#define GST_IS_SDLVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SDLVIDEOSINK))

typedef enum {
  GST_SDLVIDEOSINK_OPEN      = (GST_ELEMENT_FLAG_LAST << 0),

  GST_SDLVIDEOSINK_FLAG_LAST = (GST_ELEMENT_FLAG_LAST << 2),
} GstSDLVideoSinkFlags;

typedef struct _GstSDLVideoSink GstSDLVideoSink;
typedef struct _GstSDLVideoSinkClass GstSDLVideoSinkClass;

struct _GstSDLVideoSink {
  GstVideoSink videosink;

  guint32 format;       /* the SDL format                      */
  guint32 fourcc;       /* our fourcc from the caps            */

  gint width, height;   /* the size of the incoming YUV stream */
  unsigned long xwindow_id;
  gboolean is_xwindows;
  
  gint framerate_n;
  gint framerate_d;

  gboolean full_screen;
  gboolean init;
  gboolean running;
  GThread *event_thread;
  SDL_Surface *screen;
  SDL_Overlay *overlay;
  SDL_Rect rect;

  GMutex *lock;
};

struct _GstSDLVideoSinkClass {
  GstVideoSinkClass parent_class;

};

GType gst_sdlvideosink_get_type(void);

G_END_DECLS

#endif /* __GST_SDLVIDEOSINK_H__ */
