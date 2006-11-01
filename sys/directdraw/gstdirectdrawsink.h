/* GStreamer
 * Copyright (C)  2005 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdirectdrawsink.h: 
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


#ifndef __GST_DIRECTDRAWSINK_H__
#define __GST_DIRECTDRAWSINK_H__

#define DIRECTDRAW_VERSION 0x0700

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <windows.h>

#include <ddraw.h>

G_BEGIN_DECLS
#define GST_TYPE_DIRECTDRAW_SINK            (gst_directdrawsink_get_type())
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

  /*directdraw surface */
  LPDIRECTDRAWSURFACE surface;

  gint width;
  gint height;

  /*TRUE when surface is locked*/
  gboolean locked;
  /*TRUE when surface is using a system memory buffer 
  (i'm using system memory when directdraw optimized pitch is not the same as the GStreamer one)*/
  gboolean system_memory;

  DDPIXELFORMAT dd_pixel_format;

  GstDirectDrawSink *ddrawsink;
};


typedef struct _GstDDDDisplayMode GstDDDisplayMode;

struct _GstDDDDisplayMode
{
  gint width;
  gint height;
  gint bpp;
};

struct _GstDirectDrawSink
{
  GstVideoSink videosink;

  /*directdraw offscreen surfaces pool */
  GSList *buffer_pool;

  GSList *display_modes;
  //GstDDDisplayMode display_mode;

  /*directdraw objects */
  LPDIRECTDRAW ddraw_object;
  LPDIRECTDRAWSURFACE primary_surface;
  LPDIRECTDRAWSURFACE offscreen_surface;
  LPDIRECTDRAWSURFACE overlays;
  LPDIRECTDRAWCLIPPER clipper; 

  /*DDCAPS DDDriverCaps;
  DDCAPS DDHELCaps;
  gboolean can_blit;*/

  /*Directdraw caps */
  GstCaps *caps;

  /*handle of the video window */
  HWND video_window;
  HANDLE window_created_signal;
  gboolean resize_window;

  /*video properties */
  gint video_width, video_height;
  gint out_width, out_height;
  //gdouble framerate;
  gint fps_n;
  gint fps_d;

  /*properties*/
  LPDIRECTDRAWSURFACE extern_surface;
  gboolean keep_aspect_ratio;
  gboolean fullscreen;

  /*pixel format */
  DDPIXELFORMAT dd_pixel_format;

  GThread *window_thread;

  gboolean bUseOverlay;
  gboolean bIsOverlayVisible;
  gboolean setup;

  GMutex *pool_lock;

  guint color_key;
  /*LPDIRECTDRAWSURFACE extern_surface; */
};

struct _GstDirectDrawSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_directdrawsink_get_type (void);

G_END_DECLS
#endif /* __GST_DIRECTDRAWSINK_H__ */
