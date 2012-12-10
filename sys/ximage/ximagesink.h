/* GStreamer
 * Copyright (C) <2005> Julien Moutte <julien@moutte.net>
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

#ifndef __GST_XIMAGESINK_H__
#define __GST_XIMAGESINK_H__

#include <gst/video/gstvideosink.h>

#ifdef HAVE_XSHM
#include <sys/types.h>
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

/* Helper functions */
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_XIMAGESINK \
  (gst_ximagesink_get_type())
#define GST_XIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XIMAGESINK, GstXImageSink))
#define GST_XIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XIMAGESINK, GstXImageSinkClass))
#define GST_IS_XIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XIMAGESINK))
#define GST_IS_XIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XIMAGESINK))

typedef struct _GstXContext GstXContext;
typedef struct _GstXWindow GstXWindow;

typedef struct _GstXImageSink GstXImageSink;
typedef struct _GstXImageSinkClass GstXImageSinkClass;

#include "ximagepool.h"

/*
 * GstXContext:
 * @disp: the X11 Display of this context
 * @screen: the default Screen of Display @disp
 * @screen_num: the Screen number of @screen
 * @visual: the default Visual of Screen @screen
 * @root: the root Window of Display @disp
 * @white: the value of a white pixel on Screen @screen
 * @black: the value of a black pixel on Screen @screen
 * @depth: the color depth of Display @disp
 * @bpp: the number of bits per pixel on Display @disp
 * @endianness: the endianness of image bytes on Display @disp
 * @width: the width in pixels of Display @disp
 * @height: the height in pixels of Display @disp
 * @widthmm: the width in millimeters of Display @disp
 * @heightmm: the height in millimeters of Display @disp
 * @par: the pixel aspect ratio calculated from @width, @widthmm and @height, 
 * @heightmm ratio
 * @use_xshm: used to known wether of not XShm extension is usable or not even
 * if the Extension is present
 * @caps: the #GstCaps that Display @disp can accept
 *
 * Structure used to store various informations collected/calculated for a
 * Display.
 */
struct _GstXContext
{
  Display *disp;

  Screen *screen;
  gint screen_num;

  Visual *visual;

  Window root;

  gulong white, black;

  gint depth;
  gint bpp;

  gint width, height;
  gint widthmm, heightmm;
  GValue *par;                  /* calculated pixel aspect ratio */

  gboolean use_xshm;

  GstCaps *caps;
  GstCaps *last_caps;
};

/*
 * GstXWindow:
 * @win: the Window ID of this X11 window
 * @width: the width in pixels of Window @win
 * @height: the height in pixels of Window @win
 * @internal: used to remember if Window @win was created internally or passed
 * through the #GstVideoOverlay interface
 * @gc: the Graphical Context of Window @win
 *
 * Structure used to store informations about a Window.
 */
struct _GstXWindow
{
  Window win;
  gint width, height;
  gboolean internal;
  GC gc;
};

/**
 * GstXImageSink:
 * @display_name: the name of the Display we want to render to
 * @xcontext: our instance's #GstXContext
 * @xwindow: the #GstXWindow we are rendering to
 * @ximage: internal #GstXImage used to store incoming buffers and render when
 * not using the buffer_alloc optimization mechanism
 * @cur_image: a reference to the last #GstXImage that was put to @xwindow. It
 * is used when Expose events are received to redraw the latest video frame
 * @event_thread: a thread listening for events on @xwindow and handling them
 * @running: used to inform @event_thread if it should run/shutdown
 * @fps_n: the framerate fraction numerator
 * @fps_d: the framerate fraction denominator
 * @x_lock: used to protect X calls as we are not using the XLib in threaded
 * mode
 * @flow_lock: used to protect data flow routines from external calls such as
 * events from @event_thread or methods from the #GstVideoOverlay interface
 * @par: used to override calculated pixel aspect ratio from @xcontext
 * @pool_lock: used to protect the buffer pool
 * @buffer_pool: a list of #GstXImageBuffer that could be reused at next buffer
 * allocation call
 * @synchronous: used to store if XSynchronous should be used or not (for 
 * debugging purpose only)
 * @keep_aspect: used to remember if reverse negotiation scaling should respect
 * aspect ratio
 * @handle_events: used to know if we should handle select XEvents or not
 *
 * The #GstXImageSink data structure.
 */
struct _GstXImageSink
{
  /* Our element stuff */
  GstVideoSink videosink;

  char *display_name;

  GstXContext *xcontext;
  GstXWindow *xwindow;
  GstBuffer *cur_image;

  GThread *event_thread;
  gboolean running;

  GstVideoInfo info;

  /* Framerate numerator and denominator */
  gint fps_n;
  gint fps_d;

  GMutex x_lock;
  GMutex flow_lock;

  /* object-set pixel aspect ratio */
  GValue *par;

  /* the buffer pool */
  GstBufferPool *pool;

  gboolean synchronous;
  gboolean keep_aspect;
  gboolean handle_events;
  gboolean handle_expose;
  gboolean draw_border;

  /* stream metadata */
  gchar *media_title;
};

struct _GstXImageSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_ximagesink_get_type (void);

G_END_DECLS
#endif /* __GST_XIMAGESINK_H__ */
