/* GStreamer
 * Copyright (C) 2004 Zaheer Abbas Merali <zaheerabbas at merali dot org>
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
 
#ifndef __GST_OSXVIDEOSINK_H__
#define __GST_OSXVIDEOSINK_H__


#include <gst/gst.h>
#include <gst/video/gstvideosink.h>

#include <string.h>
#include <math.h>

#define HAVE_OSX
#ifdef HAVE_OSX
#include <QuickTime/QuickTime.h>
#import "cocoawindow.h"
#endif

G_BEGIN_DECLS

#define GST_TYPE_OSXVIDEOSINK \
  (gst_osxvideosink_get_type())
#define GST_OSXVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OSXVIDEOSINK, GstOSXVideoSink))
#define GST_OSXVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OSXVIDEOSINK, GstOSXVideoSinkClass))
#define GST_IS_OSXVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OSXVIDEOSINK))
#define GST_IS_OSXVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OSXVIDEOSINK))

typedef struct _GstOSXWindow GstOSXWindow;
typedef struct _GstOSXImage GstOSXImage;

typedef struct _GstOSXVideoSink GstOSXVideoSink;
typedef struct _GstOSXVideoSinkClass GstOSXVideoSinkClass;

/* OSXWindow stuff */
struct _GstOSXWindow {
  gint width, height;
  gboolean internal;
#ifdef HAVE_OSX
  GstWindow* win;
  GstGLView* gstview;
#endif
};

struct _GstOSXVideoSink {
  /* Our element stuff */
  GstVideoSink videosink;

  GstOSXWindow *osxwindow;
  GstOSXImage *osximage;
  GstOSXImage *cur_image;
  
  int width;
  int height;
  guint32 format;

  gint fps_n;
  gint fps_d;
  
  /* Unused */
  gint pixel_width, pixel_height;
 
  GstClockTime time;
  
  gboolean embed;
  gboolean fullscreen; 
  gboolean sw_scaling_failed;
};

struct _GstOSXVideoSinkClass {
  GstVideoSinkClass parent_class;

  /* signal callbacks */
  void (*view_created) (GstElement* element, gpointer view);
};

GType gst_osxvideosink_get_type(void);

G_END_DECLS

#endif /* __GST_OSXVIDEOSINK_H__ */
