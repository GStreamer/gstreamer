/* GStreamer
 * Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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
 
#ifndef __GST_XIMAGESINK_H__
#define __GST_XIMAGESINK_H__

#include <gst/video/gstvideosink.h>

#ifdef HAVE_XSHM
#include <sys/ipc.h>
#include <sys/shm.h>
#endif /* HAVE_XSHM */

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef HAVE_XSHM
#include <X11/extensions/XShm.h>
#endif /* HAVE_XSHM */

#include <string.h>
#include <math.h>

G_BEGIN_DECLS

#define GST_TYPE_XIMAGESINK \
  (gst_ximagesink_get_type())
#define GST_XIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XIMAGESINK, GstXImageSink))
#define GST_XIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XIMAGESINK, GstXImageSink))
#define GST_IS_XIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XIMAGESINK))
#define GST_IS_XIMAGESINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XIMAGESINK))

typedef struct _GstXContext GstXContext;
typedef struct _GstXWindow GstXWindow;
typedef struct _GstXImage GstXImage;

typedef struct _GstXImageSink GstXImageSink;
typedef struct _GstXImageSinkClass GstXImageSinkClass;

/* Global X Context stuff */
struct _GstXContext {
  Display *disp;
  
  Screen *screen;
  gint screen_num;
  
  Visual *visual;
  
  Window root;
  
  gulong white, black;
  
  gint depth;
  gint bpp;
  gint endianness;
  
  gboolean use_xshm;
  
  GstCaps *caps;
};

/* XWindow stuff */
struct _GstXWindow {
  Window win;
  gint width, height;
  gboolean internal;
  GC gc;
};

/* XImage stuff */
struct _GstXImage {
  XImage *ximage;
  
#ifdef HAVE_XSHM
  XShmSegmentInfo SHMInfo;
#endif /* HAVE_XSHM */
  
  char *data;
  gint width, height, size;
};

struct _GstXImageSink {
  /* Our element stuff */
  GstVideoSink videosink;

  GstXContext *xcontext;
  GstXWindow *xwindow;
  GstXImage *ximage;
  
  gdouble framerate;
  GMutex *x_lock;
  
  /* Unused */
  gint pixel_width, pixel_height;
 
  GstClockTime time;
  
  GMutex *pool_lock;
  GSList *image_pool;
};

struct _GstXImageSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_ximagesink_get_type(void);

G_END_DECLS

#endif /* __GST_XIMAGESINK_H__ */
