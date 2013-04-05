/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007 Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 *
 * The development of this code was made possible due to the involvement
 * of Pioneers of the Inevitable, the creators of the Songbird Music player
 *
 */

#ifndef __GST_DIRECTDRAWSINK_H__
#define __GST_DIRECTDRAWSINK_H__

#define DIRECTDRAW_VERSION 0x0700

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/interfaces/navigation.h>

#include <windows.h>
#include <ddraw.h>

G_BEGIN_DECLS

#define GST_TYPE_DIRECTDRAW_SINK            (gst_directdraw_sink_get_type())
#define GST_DIRECTDRAW_SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRECTDRAW_SINK,GstDirectDrawSink))
#define GST_DIRECTDRAW_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRECTDRAW_SINK,GstDirectDrawSinkClass))
#define GST_IS_DIRECTDRAW_SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRECTDRAW_SINK))
#define GST_IS_DIRECTDRAW_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRECTDRAW_SINK))
typedef struct _GstDirectDrawSink GstDirectDrawSink;
typedef struct _GstDirectDrawSinkClass GstDirectDrawSinkClass;

#define GST_TYPE_DDRAWSURFACE (gst_ddrawsurface_get_type())
#define GST_IS_DDRAWSURFACE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DDRAWSURFACE))
#define GST_DDRAWSURFACE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DDRAWSURFACE, GstDDrawSurface))

typedef struct _GstDDrawSurface GstDDrawSurface;

struct _GstDDrawSurface
{
  /* Extension of GstBuffer to store directdraw surfaces */
  GstBuffer buffer;

  /* directdraw surface */
  LPDIRECTDRAWSURFACE surface;

  /* surface dimensions */
  gint width;
  gint height;

  /*TRUE when surface is locked*/
  gboolean locked;

  /*TRUE when surface is using a system memory buffer 
  (i'm using system memory when directdraw optimized pitch is not the same as the GStreamer one)*/
  gboolean system_memory;

  /* pixel format of the encapsulated surface */
  DDPIXELFORMAT dd_pixel_format;

  /* pointer to parent */
  GstDirectDrawSink *ddrawsink;
};

struct _GstDirectDrawSink
{
  GstVideoSink videosink;

  /* directdraw offscreen surfaces pool */
  GSList *buffer_pool;
  GMutex *pool_lock;

  /* directdraw objects */
  LPDIRECTDRAW ddraw_object;
  LPDIRECTDRAWSURFACE primary_surface;
  LPDIRECTDRAWSURFACE offscreen_surface;
  LPDIRECTDRAWCLIPPER clipper; 

  /* last buffer displayed (used for XOverlay interface expose method) */
  GstBuffer * last_buffer;

  /* directdraw caps */
  GstCaps *caps;

  /* video window management */
  HWND video_window;
  gboolean our_video_window;
  HANDLE window_created_signal;
  WNDPROC previous_wndproc;
  LONG_PTR previous_user_data;
  
  /* video properties */
  gint video_width, video_height;
  gint out_width, out_height;
  gint fps_n;
  gint fps_d;

  /* properties */
  gboolean keep_aspect_ratio;

  /*pixel format */
  DDPIXELFORMAT dd_pixel_format;

  /* thread processing our default window messages */
  GThread *window_thread;

  /* TRUE when directdraw object is set up */
  gboolean setup;

  /* TRUE if the hardware supports blitting from one colorspace to another */
  gboolean can_blit_between_colorspace;

  /* This flag is used to force re-creation of our offscreen surface.
   * It's needed when hardware doesn't support fourcc blit and the bit depth
   * of the current display mode changes.
   */
  gboolean must_recreate_offscreen;
};

struct _GstDirectDrawSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_directdraw_sink_get_type (void);

G_END_DECLS

#endif /* __GST_DIRECTDRAWSINK_H__ */
