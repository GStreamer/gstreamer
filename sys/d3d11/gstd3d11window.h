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
#include "gstd3d11overlaycompositor.h"
#include "gstd3d11videoprocessor.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_WINDOW             (gst_d3d11_window_get_type())
#define GST_D3D11_WINDOW(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_WINDOW, GstD3D11Window))
#define GST_D3D11_WINDOW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_D3D11_WINDOW, GstD3D11WindowClass))
#define GST_IS_D3D11_WINDOW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_WINDOW))
#define GST_IS_D3D11_WINDOW_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D11_WINDOW))
#define GST_D3D11_WINDOW_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D11_WINDOW, GstD3D11WindowClass))
#define GST_D3D11_WINDOW_TOGGLE_MODE_GET_TYPE (gst_d3d11_window_fullscreen_toggle_mode_type())

typedef struct _GstD3D11Window        GstD3D11Window;
typedef struct _GstD3D11WindowClass   GstD3D11WindowClass;

#define GST_D3D11_WINDOW_FLOW_CLOSED GST_FLOW_CUSTOM_ERROR

typedef enum
{
  GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE = 0,
  GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER = (1 << 1),
  GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY = (1 << 2),
} GstD3D11WindowFullscreenToggleMode;

GType gst_d3d11_window_fullscreen_toggle_mode_type (void);

typedef enum
{
  GST_D3D11_WINDOW_NATIVE_TYPE_NONE = 0,
  GST_D3D11_WINDOW_NATIVE_TYPE_HWND,
  GST_D3D11_WINDOW_NATIVE_TYPE_CORE_WINDOW,
  GST_D3D11_WINDOW_NATIVE_TYPE_SWAP_CHAIN_PANEL,
} GstD3D11WindowNativeType;

struct _GstD3D11Window
{
  GstObject parent;

  /*< protected >*/
  gboolean initialized;
  GstD3D11Device *device;
  guintptr external_handle;

  /* properties */
  gboolean force_aspect_ratio;
  gboolean enable_navigation_events;
  GstD3D11WindowFullscreenToggleMode fullscreen_toggle_mode;
  gboolean requested_fullscreen;
  gboolean fullscreen;

  GstVideoInfo info;
  GstVideoInfo render_info;
  GstD3D11VideoProcessor *processor;
  GstD3D11ColorConverter *converter;
  GstD3D11OverlayCompositor *compositor;

  /* calculated rect with aspect ratio and window area */
  RECT render_rect;

  /* input resolution */
  RECT input_rect;

  /* requested rect via gst_d3d11_window_render */
  GstVideoRectangle rect;

  guint surface_width;
  guint surface_height;

  IDXGISwapChain *swap_chain;
  ID3D11RenderTargetView *rtv;
  ID3D11VideoProcessorOutputView *pov;
  DXGI_FORMAT dxgi_format;

  GstBuffer *cached_buffer;
  gboolean first_present;
  gboolean allow_tearing;
};

struct _GstD3D11WindowClass
{
  GstObjectClass object_class;

  void          (*show)                   (GstD3D11Window * window);

  void          (*update_swap_chain)      (GstD3D11Window * window);

  void          (*change_fullscreen_mode) (GstD3D11Window * window);

  gboolean      (*create_swap_chain)      (GstD3D11Window * window,
                                           DXGI_FORMAT format,
                                           guint width,
                                           guint height,
                                           guint swapchain_flags,
                                           IDXGISwapChain ** swap_chain);

  GstFlowReturn (*present)                (GstD3D11Window * window,
                                           guint present_flags);

  gboolean      (*unlock)                 (GstD3D11Window * window);

  gboolean      (*unlock_stop)            (GstD3D11Window * window);

  void          (*on_resize)              (GstD3D11Window * window,
                                           guint width,
                                           guint height);

  void          (*unprepare)              (GstD3D11Window * window);
};

GType         gst_d3d11_window_get_type             (void);

void          gst_d3d11_window_show                 (GstD3D11Window * window);

void          gst_d3d11_window_set_render_rectangle (GstD3D11Window * window,
                                                     gint x, gint y,
                                                     gint width, gint height);

gboolean      gst_d3d11_window_prepare              (GstD3D11Window * window,
                                                     guint display_width,
                                                     guint display_height,
                                                     GstCaps * caps,
                                                     gboolean * video_processor_available,
                                                     GError ** error);

GstFlowReturn gst_d3d11_window_render               (GstD3D11Window * window,
                                                     GstBuffer * buffer,
                                                     GstVideoRectangle * src_rect);

gboolean      gst_d3d11_window_unlock               (GstD3D11Window * window);

gboolean      gst_d3d11_window_unlock_stop          (GstD3D11Window * window);

void          gst_d3d11_window_unprepare            (GstD3D11Window * window);

void          gst_d3d11_window_on_key_event         (GstD3D11Window * window,
                                                     const gchar * event,
                                                     const gchar * key);

void          gst_d3d11_window_on_mouse_event       (GstD3D11Window * window,
                                                     const gchar * event,
                                                     gint button,
                                                     gdouble x,
                                                     gdouble y);

/* utils */
GstD3D11WindowNativeType gst_d3d11_window_get_native_type_from_handle (guintptr handle);

const gchar *            gst_d3d11_window_get_native_type_to_string   (GstD3D11WindowNativeType type);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstD3D11Window, gst_object_unref)

G_END_DECLS

#endif /* __GST_D3D11_WINDOW_H__ */
