/* GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
 * Copyright (C) 2005 Julien Moutte <julien@moutte.net>
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

#ifndef __GST_VDP_SINK_H__
#define __GST_VDP_SINK_H__

#include <gst/video/gstvideosink.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <string.h>
#include <math.h>

#include "gstvdpdevice.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_SINK \
  (gst_vdp_sink_get_type())
#define GST_VDP_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VDP_SINK, VdpSink))
#define GST_VDP_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VDP_SINK, VdpSinkClass))
#define GST_IS_VDP_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VDP_SINK))
#define GST_IS_VDP_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VDP_SINK))

typedef struct _GstXContext GstXContext;
typedef struct _GstVdpWindow GstVdpWindow;

typedef struct _VdpSink VdpSink;
typedef struct _VdpSinkClass VdpSinkClass;

/*
 * GstVdpWindow:
 * @win: the Window ID of this X11 window
 * @target the VdpPresentationQueueTarget of this window
 * @queue the VdpPresentationQueue of this window
 * @width: the width in pixels of Window @win
 * @height: the height in pixels of Window @win
 * @internal: used to remember if Window @win was created internally or passed
 * through the #GstXOverlay interface
 *
 * Structure used to store informations about a Window.
 */
struct _GstVdpWindow {
  Window win;
  VdpPresentationQueueTarget target;
  VdpPresentationQueue queue;
  gint width, height;
  gboolean internal;
};

/**
 * VdpSink:
 * @display_name: the name of the Display we want to render to
 * @device: the GstVdpDevice associated with the display_name
 * @window: the #GstVdpWindow we are rendering to
 * @cur_image: a reference to the last #GstBuffer that was put to @window. It
 * is used when Expose events are received to redraw the latest video frame
 * @event_thread: a thread listening for events on @window and handling them
 * @running: used to inform @event_thread if it should run/shutdown
 * @fps_n: the framerate fraction numerator
 * @fps_d: the framerate fraction denominator
 * @x_lock: used to protect X calls as we are not using the XLib in threaded
 * mode
 * @flow_lock: used to protect data flow routines from external calls such as
 * events from @event_thread or methods from the #GstXOverlay interface
 * @par: used to override calculated pixel aspect ratio from @xcontext
 * @synchronous: used to store if XSynchronous should be used or not (for
 * debugging purpose only)
 * @handle_events: used to know if we should handle select XEvents or not
 *
 * The #VdpSink data structure.
 */
struct _VdpSink {
  /* Our element stuff */
  GstVideoSink videosink;

  char *display_name;

  GstVdpDevice *device;
  GstBufferPool *bpool;
  GstCaps *caps;
  
  GstVdpWindow *window;
  GstBuffer *cur_image;
  
  GThread *event_thread;
  gboolean running;

  /* Framerate numerator and denominator */
  gint fps_n;
  gint fps_d;

  GMutex *device_lock;
  GMutex *x_lock;
  GMutex *flow_lock;
  
  /* object-set pixel aspect ratio */
  GValue *par;

  gboolean synchronous;
  gboolean handle_events;
  gboolean handle_expose;
  
  /* stream metadata */
  gchar *media_title;
};

struct _VdpSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_vdp_sink_get_type(void);

G_END_DECLS

#endif /* __GST_VDP_SINK_H__ */
