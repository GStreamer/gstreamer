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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_XVIMAGESINK_H__
#define __GST_XVIMAGESINK_H__

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

#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include <string.h>
#include <math.h>
#include <stdlib.h>

/* Helper functions */
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_XVIMAGESINK \
  (gst_xvimagesink_get_type())
#define GST_XVIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XVIMAGESINK, GstXvImageSink))
#define GST_XVIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XVIMAGESINK, GstXvImageSinkClass))
#define GST_IS_XVIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XVIMAGESINK))
#define GST_IS_XVIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XVIMAGESINK))
typedef struct _GstXContext GstXContext;
typedef struct _GstXWindow GstXWindow;
typedef struct _GstXvImageFormat GstXvImageFormat;

typedef struct _GstXvImageSink GstXvImageSink;
typedef struct _GstXvImageSinkClass GstXvImageSinkClass;

#include "xvimagepool.h"

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
 * @xv_port_id: the XVideo port ID
 * @im_format: used to store at least a valid format for XShm calls checks
 * @formats_list: list of supported image formats on @xv_port_id
 * @channels_list: list of #GstColorBalanceChannels
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
  gint endianness;

  gint width, height;
  gint widthmm, heightmm;
  GValue *par;                  /* calculated pixel aspect ratio */

  gboolean use_xshm;

  XvPortID xv_port_id;
  guint nb_adaptors;
  gchar **adaptors;
  gint im_format;

  GList *formats_list;
  GList *channels_list;

  GstCaps *caps;

  /* Optimisation storage for buffer_alloc return */
  GstCaps *last_caps;
  gint last_format;
  gint last_width;
  gint last_height;
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
 * GstXvImageFormat:
 * @format: the image format
 * @caps: generated #GstCaps for this image format
 *
 * Structure storing image format to #GstCaps association.
 */
struct _GstXvImageFormat
{
  gint format;
  GstVideoFormat vformat;
  GstCaps *caps;
};


/**
 * GstXvImageSink:
 * @display_name: the name of the Display we want to render to
 * @xcontext: our instance's #GstXContext
 * @xwindow: the #GstXWindow we are rendering to
 * @cur_image: a reference to the last #GstXvImage that was put to @xwindow. It
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
 * @image_pool: a list of #GstXvImageBuffer that could be reused at next buffer
 * allocation call
 * @synchronous: used to store if XSynchronous should be used or not (for
 * debugging purpose only)
 * @keep_aspect: used to remember if reverse negotiation scaling should respect
 * aspect ratio
 * @handle_events: used to know if we should handle select XEvents or not
 * @brightness: used to store the user settings for color balance brightness
 * @contrast: used to store the user settings for color balance contrast
 * @hue: used to store the user settings for color balance hue
 * @saturation: used to store the user settings for color balance saturation
 * @cb_changed: used to store if the color balance settings where changed
 * @video_width: the width of incoming video frames in pixels
 * @video_height: the height of incoming video frames in pixels
 *
 * The #GstXvImageSink data structure.
 */
struct _GstXvImageSink
{
  /* Our element stuff */
  GstVideoSink videosink;

  char *display_name;
  guint adaptor_no;

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
  gboolean double_buffer;
  gboolean keep_aspect;
  gboolean redraw_border;
  gboolean handle_events;
  gboolean handle_expose;

  gint brightness;
  gint contrast;
  gint hue;
  gint saturation;
  gboolean cb_changed;

  /* size of incoming video, used as the size for XvImage */
  guint video_width, video_height;

  /* port attributes */
  gboolean autopaint_colorkey;
  gint colorkey;

  gboolean draw_borders;

  /* port features */
  gboolean have_autopaint_colorkey;
  gboolean have_colorkey;
  gboolean have_double_buffer;

  /* stream metadata */
  gchar *media_title;

  /* target video rectangle */
  GstVideoRectangle render_rect;
  gboolean have_render_rect;
};

struct _GstXvImageSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_xvimagesink_get_type (void);

G_END_DECLS
#endif /* __GST_XVIMAGESINK_H__ */
