/* GStreamer
 *
 * Copyright (C) 2011 - Collabora Ltda
 * Copyright (C) 2011 - Texas Instruments
 *  @author: Luciana Fujii Pontello <luciana.fujii@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
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

#ifndef __GST_PVRVIDEOSINK_H__
#define __GST_PVRVIDEOSINK_H__

#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include <string.h>
#include <math.h>
#include <pvr2d.h>
#include <EGL/egl.h>
#include <wsegl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

G_BEGIN_DECLS
#define GST_TYPE_PVRVIDEOSINK (gst_pvrvideosink_get_type())
#define GST_PVRVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PVRVIDEOSINK, GstPVRVideoSink))
#define GST_PVRVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PVRVIDEOSINK, GstPVRVideoSinkClass))
#define GST_IS_PVRVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PVRVIDEOSINK))
#define GST_IS_PVRVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_PVRVIDEOSINK))
typedef struct _GstDrawContext GstDrawContext;
typedef struct _GstXWindow GstXWindow;

typedef struct _GstPVRVideoBuffer GstPVRVideoBuffer;
typedef struct _GstPVRVideoBufferClass GstPVRVideoBufferClass;

typedef struct _GstPVRVideoSink GstPVRVideoSink;
typedef struct _GstPVRVideoSinkClass GstPVRVideoSinkClass;

struct _GstDrawContext
{
  /* PVR2D */
  PVR2DCONTEXTHANDLE pvr_context;
  PVR2DMEMINFO dst_mem;
  PPVR2D_3DBLT_EXT p_blt_info;
  PPVR2DBLTINFO p_blt2d_info;

  long stride;
  PVR2DFORMAT display_format;
  long display_width;
  long display_height;

  /* WSEGL */
  const WSEGL_FunctionTable *wsegl_table;

  WSEGLDisplayHandle display_handle;
  const WSEGLCaps **glcaps;
  WSEGLConfig *glconfig;
  WSEGLDrawableHandle drawable_handle;
  WSEGLRotationAngle rotation;

  GMutex *x_lock;
  Display *x_display;
  gint screen_num;
  gulong black;
};

struct _GstXWindow
{
  Window window;
  gint width, height;
  gboolean internal;
  GC gc;
};


/**
 * GstPVRVideoSink:
 * @running: used to inform @event_thread if it should run/shutdown
 * @fps_n: the framerate fraction numerator
 * @fps_d: the framerate fraction denominator
 * @flow_lock: used to protect data flow routines from external calls such as
 * events from @event_thread or methods from the #GstXOverlay interface
 * @x_lock: used to protect X calls
 * @buffer_pool: a list of #GstPVRVideoBuffer that could be reused at next buffer
 * allocation call
 * @keep_aspect: used to remember if reverse negotiation scaling should respect
 * aspect ratio
 *
 * The #GstPVRVideoSink data structure.
 */
struct _GstPVRVideoSink
{
  /* Our element stuff */
  GstVideoSink videosink;

  gboolean running;

  /* Framerate numerator and denominator */
  GstVideoInfo info;

  GThread *event_thread;
  GMutex *flow_lock;

  GstBufferPool *pool;

  gboolean keep_aspect;

  GstCaps *current_caps;
  GstDrawContext *dcontext;
  GstXWindow *xwindow;

  GstVideoRectangle render_rect;
  gboolean have_render_rect;

  gchar *media_title;
  gboolean redraw_borders;
  GstBuffer *current_buffer;

  /* List of buffer using GstPVRMeta on ourselves */
  GList *metabuffers;

  WSEGLDrawableParams render_params;
};

struct _GstPVRVideoSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_pvrvideosink_get_type (void);

void gst_pvrvideosink_track_buffer (GstPVRVideoSink * pvrsink, GstBuffer * buffer);
void gst_pvrvideosink_untrack_buffer (GstPVRVideoSink * pvrsink, GstBuffer * buffer);

G_END_DECLS
#endif /* __GST_PVRVIDEOSINK_H__ */
