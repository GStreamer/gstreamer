/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_WINDOW_H__
#define __GST_D3D11_WINDOW_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstd3d11_fwd.h"
#include "gstd3d11colorconverter.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_WINDOW             (gst_d3d11_window_get_type())
#define GST_D3D11_WINDOW(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_D3D11_WINDOW, GstD3D11Window))
#define GST_D3D11_WINDOW_CLASS(klass)     (G_TYPE_CHECK_CLASS((klass), GST_TYPE_D3D11_WINDOW, GstD3D11WindowClass))
#define GST_IS_D3D11_WINDOW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_D3D11_WINDOW))
#define GST_IS_D3D11_WINDOW_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D11_WINDOW))
#define GST_D3D11_WINDOW_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D11_WINDOW, GstD3D11WindowClass))

typedef struct _GstD3D11Window        GstD3D11Window;
typedef struct _GstD3D11WindowClass   GstD3D11WindowClass;

#define GST_D3D11_WINDOW_FLOW_CLOSED GST_FLOW_CUSTOM_ERROR

typedef enum
{
  GST_D3D11_WINDOW_OVERLAY_STATE_NONE = 0,
  GST_D3D11_WINDOW_OVERLAY_STATE_OPENED,
  GST_D3D11_WINDOW_OVERLAY_STATE_CLOSED,
} GstD3D11WindowOverlayState;

struct _GstD3D11Window
{
  GstObject parent;

  GstVideoInfo info;
  GstVideoInfo render_info;
  const GstD3D11Format *render_format;
  GstD3D11ColorConverter *converter;

  GstVideoMasteringDisplayInfo mastering_display_info;
  GstVideoContentLightLevel content_light_level;

  /* calculated rect with aspect ratio and window area */
  GstVideoRectangle render_rect;

  /* requested rect via gst_d3d11_window_render */
  GstVideoRectangle rect;

  GMutex lock;
  GCond cond;

  GMainContext *main_context;
  GMainLoop *loop;

  guint width;
  guint height;

  guint surface_width;
  guint surface_height;

  guint aspect_ratio_n;
  guint aspect_ratio_d;

  gboolean visible;

  GSource *msg_source;
  GIOChannel *msg_io_channel;

  GThread *thread;

  gboolean created;

  HWND internal_win_id;
  HWND external_win_id;
  GstD3D11WindowOverlayState overlay_state;

  HDC device_handle;
  IDXGISwapChain *swap_chain;
  ID3D11RenderTargetView *rtv;
  DXGI_FORMAT format;

  GstD3D11Device *device;

  gboolean pending_resize;

  gboolean force_aspect_ratio;
  gboolean enable_navigation_events;

  GstBuffer *cached_buffer;
  gboolean allow_tearing;
};

struct _GstD3D11WindowClass
{
  GstObjectClass object_class;
};

GType             gst_d3d11_window_get_type     (void);

GstD3D11Window *  gst_d3d11_window_new (GstD3D11Device * device);

void              gst_d3d11_window_show (GstD3D11Window * window);

void              gst_d3d11_window_set_window_handle (GstD3D11Window * window,
                                                      guintptr id);

void              gst_d3d11_window_set_render_rectangle (GstD3D11Window * window,
                                                         gint x, gint y,
                                                         gint width, gint height);

void gst_d3d11_window_get_surface_dimensions (GstD3D11Window * window,
                                         guint * width,
                                         guint * height);

gboolean gst_d3d11_window_prepare (GstD3D11Window * window,
                                   guint width,
                                   guint height,
                                   guint aspect_ratio_n,
                                   guint aspect_ratio_d,
                                   GstCaps * caps,
                                   GError ** error);

GstFlowReturn gst_d3d11_window_render (GstD3D11Window * window,
                                       GstBuffer * buffer,
                                       GstVideoRectangle * src_rect);

gboolean      gst_d3d11_window_flush  (GstD3D11Window * window);

G_END_DECLS

#endif /* __GST_D3D11_WINDOW_H__ */
