/*
 * GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d11window.h"
#include "gstd3d11device.h"
#include "gstd3d11memory.h"
#include "gstd3d11utils.h"

#if GST_D3D11_WINAPI_ONLY_APP
/* workaround for GetCurrentTime collision */
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <windows.ui.xaml.h>
#include <windows.applicationmodel.core.h>
#endif

#ifdef HAVE_DIRECT_WRITE
#include <dwrite.h>
#include <d2d1_1.h>
#include <sstream>
#endif

#if GST_D3D11_WINAPI_ONLY_APP || defined(HAVE_DIRECT_WRITE)
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
#endif

extern "C" {
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_window_debug);
#define GST_CAT_DEFAULT gst_d3d11_window_debug
}

struct _GstD3D11WindowPrivate
{
#ifdef HAVE_DIRECT_WRITE
  IDWriteFactory *dwrite_factory;
  IDWriteTextFormat *dwrite_format;

  ID2D1Factory1 *d2d_factory;
  ID2D1Device *d2d_device;
  ID2D1DeviceContext *d2d_device_context;
  ID2D1SolidColorBrush *d2d_brush;
#else
  gpointer dummy;
#endif
};

enum
{
  PROP_0,
  PROP_D3D11_DEVICE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS,
  PROP_FULLSCREEN_TOGGLE_MODE,
  PROP_FULLSCREEN,
  PROP_WINDOW_HANDLE,
  PROP_RENDER_STATS,
};

#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE
#define DEFAULT_FULLSCREEN_TOGGLE_MODE    GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE
#define DEFAULT_FULLSCREEN                FALSE
#define DEFAULT_RENDER_STATS              FALSE

enum
{
  SIGNAL_KEY_EVENT,
  SIGNAL_MOUSE_EVENT,
  SIGNAL_LAST
};

static guint d3d11_window_signals[SIGNAL_LAST] = { 0, };

GType
gst_d3d11_window_fullscreen_toggle_mode_type (void)
{
  static volatile gsize mode_type = 0;

  if (g_once_init_enter (&mode_type)) {
    static const GFlagsValue mode_types[] = {
      {GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE,
          "GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE", "none"},
      {GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER,
          "GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER", "alt-enter"},
      {GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY,
          "GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY", "property"},
      {0, NULL, NULL},
    };
    GType tmp = g_flags_register_static ("GstD3D11WindowFullscreenToggleMode",
        mode_types);
    g_once_init_leave (&mode_type, tmp);
  }

  return (GType) mode_type;
}

#define gst_d3d11_window_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstD3D11Window, gst_d3d11_window,
    GST_TYPE_OBJECT);

static void gst_d3d11_window_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_window_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_window_dispose (GObject * object);
static GstFlowReturn gst_d3d111_window_present (GstD3D11Window * self,
    GstBuffer * buffer, GstStructure * stats);
static void gst_d3d11_window_on_resize_default (GstD3D11Window * window,
    guint width, guint height);

static void
gst_d3d11_window_class_init (GstD3D11WindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_window_set_property;
  gobject_class->get_property = gst_d3d11_window_get_property;
  gobject_class->dispose = gst_d3d11_window_dispose;

  klass->on_resize = GST_DEBUG_FUNCPTR (gst_d3d11_window_on_resize_default);

  g_object_class_install_property (gobject_class, PROP_D3D11_DEVICE,
      g_param_spec_object ("d3d11device", "D3D11 Device",
          "GstD3D11Device object for creating swapchain",
          GST_TYPE_D3D11_DEVICE,
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ENABLE_NAVIGATION_EVENTS,
      g_param_spec_boolean ("enable-navigation-events",
          "Enable navigation events",
          "When enabled, signals for navigation events are emitted",
          DEFAULT_ENABLE_NAVIGATION_EVENTS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN_TOGGLE_MODE,
      g_param_spec_flags ("fullscreen-toggle-mode",
          "Full screen toggle mode",
          "Full screen toggle mode used to trigger fullscreen mode change",
          GST_D3D11_WINDOW_TOGGLE_MODE_GET_TYPE, DEFAULT_FULLSCREEN_TOGGLE_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen",
          "fullscreen",
          "Ignored when \"fullscreen-toggle-mode\" does not include \"property\"",
          DEFAULT_FULLSCREEN,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_WINDOW_HANDLE,
      g_param_spec_pointer ("window-handle",
          "Window Handle", "External Window Handle",
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
              G_PARAM_STATIC_STRINGS)));

#ifdef HAVE_DIRECT_WRITE
  g_object_class_install_property (gobject_class, PROP_RENDER_STATS,
      g_param_spec_boolean ("render-stats",
          "Render Stats",
          "Render statistics data (e.g., average framerate) on window",
          DEFAULT_RENDER_STATS,
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)));
#endif

  d3d11_window_signals[SIGNAL_KEY_EVENT] =
      g_signal_new ("key-event", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  d3d11_window_signals[SIGNAL_MOUSE_EVENT] =
      g_signal_new ("mouse-event", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
}

static void
gst_d3d11_window_init (GstD3D11Window * self)
{
  self->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  self->enable_navigation_events = DEFAULT_ENABLE_NAVIGATION_EVENTS;
  self->fullscreen_toggle_mode = GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE;
  self->fullscreen = DEFAULT_FULLSCREEN;
  self->render_stats = DEFAULT_RENDER_STATS;

  self->priv =
      (GstD3D11WindowPrivate *) gst_d3d11_window_get_instance_private (self);
}

static void
gst_d3d11_window_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);
  GstD3D11WindowClass *klass = GST_D3D11_WINDOW_GET_CLASS (object);

  switch (prop_id) {
    case PROP_D3D11_DEVICE:
      self->device = (GstD3D11Device *) g_value_dup_object (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
    {
      self->force_aspect_ratio = g_value_get_boolean (value);
      if (self->swap_chain)
        klass->update_swap_chain (self);
      break;
    }
    case PROP_ENABLE_NAVIGATION_EVENTS:
      self->enable_navigation_events = g_value_get_boolean (value);
      break;
    case PROP_FULLSCREEN_TOGGLE_MODE:
      self->fullscreen_toggle_mode =
          (GstD3D11WindowFullscreenToggleMode) g_value_get_flags (value);
      break;
    case PROP_FULLSCREEN:
    {
      self->requested_fullscreen = g_value_get_boolean (value);
      if (self->swap_chain)
        klass->change_fullscreen_mode (self);
      break;
    }
    case PROP_WINDOW_HANDLE:
      self->external_handle = (guintptr) g_value_get_pointer (value);
      break;
#ifdef HAVE_DIRECT_WRITE
    case PROP_RENDER_STATS:
      self->render_stats = g_value_get_boolean (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_window_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  switch (prop_id) {
    case PROP_ENABLE_NAVIGATION_EVENTS:
      g_value_set_boolean (value, self->enable_navigation_events);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, self->force_aspect_ratio);
      break;
    case PROP_FULLSCREEN_TOGGLE_MODE:
      g_value_set_flags (value, self->fullscreen_toggle_mode);
      break;
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, self->fullscreen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifdef HAVE_DIRECT_WRITE
static void
gst_d3d11_window_release_dwrite_resources (GstD3D11Window * self)
{
  GstD3D11WindowPrivate *priv = self->priv;

  if (priv->d2d_device_context) {
    priv->d2d_device_context->Release ();
    priv->d2d_device_context = NULL;
  }

  if (priv->d2d_factory) {
    priv->d2d_factory->Release ();
    priv->d2d_factory = NULL;
  }

  if (priv->d2d_device) {
    priv->d2d_device->Release ();
    priv->d2d_device = NULL;
  }

  if (priv->d2d_brush) {
    priv->d2d_brush->Release ();
    priv->d2d_brush = NULL;
  }

  if (priv->dwrite_factory) {
    priv->dwrite_factory->Release ();
    priv->dwrite_factory = NULL;
  }

  if (priv->dwrite_format) {
    priv->dwrite_format->Release ();
    priv->dwrite_format = NULL;
  }
}

static void
gst_d3d11_window_prepare_dwrite_device (GstD3D11Window * self)
{
  GstD3D11WindowPrivate *priv = self->priv;
  HRESULT hr;
  ComPtr<IDXGIDevice> dxgi_device;
  ID3D11Device *device_handle;

  if (!self->device) {
    GST_ERROR_OBJECT (self, "D3D11Device is unavailable");
    return;
  }

  /* Already prepared */
  if (priv->d2d_device)
    return;

  hr = D2D1CreateFactory (D2D1_FACTORY_TYPE_MULTI_THREADED,
      IID_PPV_ARGS (&priv->d2d_factory));
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  device_handle = gst_d3d11_device_get_device_handle (self->device);
  hr = device_handle->QueryInterface (IID_PPV_ARGS (&dxgi_device));
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  hr = priv->d2d_factory->CreateDevice (dxgi_device.Get (), &priv->d2d_device);
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  hr = priv->d2d_device->CreateDeviceContext (D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS,
      &priv->d2d_device_context);
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  hr = priv->d2d_device_context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow),
      &priv->d2d_brush);
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  hr = DWriteCreateFactory (DWRITE_FACTORY_TYPE_SHARED,
      __uuidof (IDWriteFactory), (IUnknown **) &priv->dwrite_factory);
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  /* Configure font */
  hr = priv->dwrite_factory->CreateTextFormat(L"Arial", NULL,
      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &priv->dwrite_format);
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  GST_DEBUG_OBJECT (self, "Direct2D device is prepared");

  return;

error:
  gst_d3d11_window_release_dwrite_resources (self);
}

static void
gst_d3d11_window_dwrite_on_resize (GstD3D11Window * self,
    ID3D11Texture2D * backbuffer)
{
  GstD3D11WindowPrivate *priv = self->priv;
  ComPtr<IDXGISurface> dxgi_surface;
  ComPtr<ID2D1Bitmap1> bitmap;
  D2D1_BITMAP_PROPERTIES1 prop;
  HRESULT hr;

  if (!priv->d2d_device)
    return;

  prop.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  prop.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
  /* default dpi */
  prop.dpiX = 96.0f;
  prop.dpiY = 96.0f;
  prop.bitmapOptions =
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
  prop.colorContext = NULL;

  hr = backbuffer->QueryInterface (IID_PPV_ARGS (&dxgi_surface));
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  hr = priv->d2d_device_context->CreateBitmapFromDxgiSurface (dxgi_surface.Get (),
      &prop, &bitmap);
  if (!gst_d3d11_result (hr, self->device))
    goto error;

  priv->d2d_device_context->SetTarget (bitmap.Get ());

  GST_LOG_OBJECT (self, "New D2D bitmap has been configured");

  return;

error:
  gst_d3d11_window_release_dwrite_resources (self);
}

#endif

static void
gst_d3d11_window_release_resources (GstD3D11Device * device,
    GstD3D11Window * window)
{
#ifdef HAVE_DIRECT_WRITE
  gst_d3d11_window_release_dwrite_resources (window);
#endif

  if (window->rtv) {
    window->rtv->Release ();
    window->rtv = NULL;
  }

  if (window->pov) {
    window->pov->Release ();
    window->pov = NULL;
  }

  if (window->swap_chain) {
    window->swap_chain->Release ();
    window->swap_chain = NULL;
  }
}

static void
gst_d3d11_window_dispose (GObject * object)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  if (self->device) {
    gst_d3d11_window_release_resources (self->device, self);
  }

  g_clear_pointer (&self->processor, gst_d3d11_video_processor_free);
  g_clear_pointer (&self->converter, gst_d3d11_color_converter_free);
  g_clear_pointer (&self->compositor, gst_d3d11_overlay_compositor_free);

  gst_clear_buffer (&self->cached_buffer);
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_window_on_resize_default (GstD3D11Window * window, guint width,
    guint height)
{
#ifdef HAVE_DIRECT_WRITE
  GstD3D11WindowPrivate *priv = window->priv;
#endif
  HRESULT hr;
  ID3D11Device *device_handle;
  D3D11_TEXTURE2D_DESC desc;
  DXGI_SWAP_CHAIN_DESC swap_desc;
  ID3D11Texture2D *backbuffer = NULL;
  GstVideoRectangle src_rect, dst_rect, rst_rect;
  IDXGISwapChain *swap_chain;

  gst_d3d11_device_lock (window->device);
  if (!window->swap_chain)
    goto done;

  device_handle = gst_d3d11_device_get_device_handle (window->device);
  swap_chain = window->swap_chain;

  if (window->rtv) {
    window->rtv->Release ();
    window->rtv = NULL;
  }

  if (window->pov) {
    window->pov->Release ();
    window->pov = NULL;
  }

#ifdef HAVE_DIRECT_WRITE
  /* D2D bitmap need to be cleared before resizing swapchain buffer */
  if (priv->d2d_device_context)
    priv->d2d_device_context->SetTarget (NULL);
#endif

  swap_chain->GetDesc (&swap_desc);
  hr = swap_chain->ResizeBuffers (0, width, height, window->dxgi_format,
      swap_desc.Flags);
  if (!gst_d3d11_result (hr, window->device)) {
    GST_ERROR_OBJECT (window, "Couldn't resize buffers, hr: 0x%x", (guint) hr);
    goto done;
  }

  hr = swap_chain->GetBuffer (0, IID_ID3D11Texture2D, (void **) &backbuffer);
  if (!gst_d3d11_result (hr, window->device)) {
    GST_ERROR_OBJECT (window,
        "Cannot get backbuffer from swapchain, hr: 0x%x", (guint) hr);
    goto done;
  }

  backbuffer->GetDesc (&desc);
  window->surface_width = desc.Width;
  window->surface_height = desc.Height;

  {
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = window->surface_width;
    dst_rect.h = window->surface_height;

    if (window->force_aspect_ratio) {
      src_rect.x = 0;
      src_rect.y = 0;
      src_rect.w = GST_VIDEO_INFO_WIDTH (&window->render_info);
      src_rect.h = GST_VIDEO_INFO_HEIGHT (&window->render_info);

      gst_video_sink_center_rect (src_rect, dst_rect, &rst_rect, TRUE);
    } else {
      rst_rect = dst_rect;
    }
  }

  window->render_rect.left = rst_rect.x;
  window->render_rect.top = rst_rect.y;
  window->render_rect.right = rst_rect.x + rst_rect.w;
  window->render_rect.bottom = rst_rect.y + rst_rect.h;

  GST_LOG_OBJECT (window,
      "New client area %dx%d, render rect x: %d, y: %d, %dx%d",
      desc.Width, desc.Height, rst_rect.x, rst_rect.y, rst_rect.w, rst_rect.h);

  hr = device_handle->CreateRenderTargetView ((ID3D11Resource *) backbuffer,
      NULL, &window->rtv);
  if (!gst_d3d11_result (hr, window->device)) {
    GST_ERROR_OBJECT (window, "Cannot create render target view, hr: 0x%x",
        (guint) hr);

    goto done;
  }

  if (window->processor) {
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC pov_desc;

    pov_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    pov_desc.Texture2D.MipSlice = 0;

    if (!gst_d3d11_video_processor_create_output_view (window->processor,
        &pov_desc, (ID3D11Resource *) backbuffer, &window->pov))
      goto done;
  }

#ifdef HAVE_DIRECT_WRITE
  if (window->render_stats)
    gst_d3d11_window_dwrite_on_resize (window, backbuffer);
#endif

  window->first_present = TRUE;

  /* redraw the last scene if cached buffer exits */
  gst_d3d111_window_present (window, NULL, NULL);

done:
  if (backbuffer)
    backbuffer->Release ();

  gst_d3d11_device_unlock (window->device);
}

void
gst_d3d11_window_on_key_event (GstD3D11Window * window, const gchar * event,
    const gchar * key)
{
  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  if (!window->enable_navigation_events)
    return;

  g_signal_emit (window, d3d11_window_signals[SIGNAL_KEY_EVENT], 0, event, key);
}

void
gst_d3d11_window_on_mouse_event (GstD3D11Window * window, const gchar * event,
    gint button, gdouble x, gdouble y)
{
  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  if (!window->enable_navigation_events)
    return;

  g_signal_emit (window, d3d11_window_signals[SIGNAL_MOUSE_EVENT], 0,
      event, button, x, y);
}

typedef struct
{
  DXGI_FORMAT dxgi_format;
  GstVideoFormat gst_format;
  gboolean supported;
} GstD3D11WindowDisplayFormat;


gboolean
gst_d3d11_window_prepare (GstD3D11Window * window, guint display_width,
    guint display_height, GstCaps * caps, gboolean * video_processor_available,
    GError ** error)
{
  GstD3D11WindowClass *klass;
  guint swapchain_flags = 0;
  ID3D11Device *device_handle;
  guint i;
  guint num_supported_format = 0;
  HRESULT hr;
  UINT display_flags =
      D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DISPLAY;
  UINT supported_flags = 0;
  GstD3D11WindowDisplayFormat formats[] = {
    { DXGI_FORMAT_R8G8B8A8_UNORM, GST_VIDEO_FORMAT_RGBA, FALSE },
    { DXGI_FORMAT_B8G8R8A8_UNORM, GST_VIDEO_FORMAT_BGRA, FALSE },
    { DXGI_FORMAT_R10G10B10A2_UNORM, GST_VIDEO_FORMAT_RGB10A2_LE, FALSE },
  };
  const GstD3D11WindowDisplayFormat *chosen_format = NULL;
  const GstDxgiColorSpace * chosen_colorspace = NULL;
#if (DXGI_HEADER_VERSION >= 4)
  gboolean have_hdr10 = FALSE;
  DXGI_COLOR_SPACE_TYPE native_colorspace_type =
      DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
#endif
#if (DXGI_HEADER_VERSION >= 5)
  DXGI_HDR_METADATA_HDR10 hdr10_metadata = { 0, };
#endif

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);

  GST_DEBUG_OBJECT (window, "Prepare window, display resolution %dx%d, caps %"
      GST_PTR_FORMAT, display_width, display_height, caps);

  /* Step 1: Clear old resources and objects */
  gst_clear_buffer (&window->cached_buffer);
  g_clear_pointer (&window->processor, gst_d3d11_video_processor_free);
  g_clear_pointer (&window->converter, gst_d3d11_color_converter_free);
  g_clear_pointer (&window->compositor, gst_d3d11_overlay_compositor_free);

  /* Step 2: Decide display color format
   * If upstream format is 10bits, try DXGI_FORMAT_R10G10B10A2_UNORM first
   * Otherwise, use DXGI_FORMAT_B8G8R8A8_UNORM or DXGI_FORMAT_B8G8R8A8_UNORM
   */
  gst_video_info_from_caps (&window->info, caps);
  device_handle = gst_d3d11_device_get_device_handle (window->device);
  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    hr = device_handle->CheckFormatSupport (formats[i].dxgi_format,
        &supported_flags);
    if (SUCCEEDED (hr) && (supported_flags & display_flags) == display_flags) {
      GST_DEBUG_OBJECT (window, "Device supports format %s (DXGI_FORMAT %d)",
          gst_video_format_to_string (formats[i].gst_format),
          formats[i].dxgi_format);
      formats[i].supported = TRUE;
      num_supported_format++;
    }
  }

  if (num_supported_format == 0) {
    GST_ERROR_OBJECT (window, "Cannot determine render format");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot determine render format");
    return FALSE;
  }

#ifdef HAVE_DIRECT_WRITE
  if (window->render_stats && formats[1].supported) {
    /* FIXME: D2D seems to be accepting only DXGI_FORMAT_B8G8R8A8_UNORM */
    chosen_format = &formats[1];
  } else
#else
  {
    for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (&window->info); i++) {
      if (GST_VIDEO_INFO_COMP_DEPTH (&window->info, i) > 8) {
        if (formats[2].supported) {
          chosen_format = &formats[2];
        }
        break;
      }
    }
  }
#endif

  if (!chosen_format) {
    /* prefer native format over conversion */
    for (i = 0; i < 2; i++) {
      if (formats[i].supported &&
          formats[i].gst_format == GST_VIDEO_INFO_FORMAT (&window->info)) {
        chosen_format = &formats[i];
        break;
      }
    }

    /* choose any color space then */
    if (!chosen_format) {
      for (i = 0; i < G_N_ELEMENTS (formats); i++) {
        if (formats[i].supported) {
          chosen_format = &formats[i];
          break;
        }
      }
    }
  }

  g_assert (chosen_format != NULL);

  GST_DEBUG_OBJECT (window, "chosen rendero format %s (DXGI_FORMAT %d)",
      gst_video_format_to_string (chosen_format->gst_format),
      chosen_format->dxgi_format);

  /* Step 3: Create swapchain
   * (or reuse old swapchain if the format is not changed) */
  window->allow_tearing = FALSE;
  g_object_get (window->device, "allow-tearing", &window->allow_tearing, NULL);
  if (window->allow_tearing) {
    GST_DEBUG_OBJECT (window, "device support tearning");
    swapchain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
  }

  gst_d3d11_device_lock (window->device);
  window->dxgi_format = chosen_format->dxgi_format;

  klass = GST_D3D11_WINDOW_GET_CLASS (window);
  if (!window->swap_chain &&
      !klass->create_swap_chain (window, window->dxgi_format,
          display_width, display_height, swapchain_flags, &window->swap_chain)) {
    GST_ERROR_OBJECT (window, "Cannot create swapchain");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create swapchain");
    goto error;
  }

  /* this rect struct will be used to calculate render area */
  window->render_rect.left = 0;
  window->render_rect.top = 0;
  window->render_rect.right = display_width;
  window->render_rect.bottom = display_height;

  window->input_rect.left = 0;
  window->input_rect.top = 0;
  window->input_rect.right = GST_VIDEO_INFO_WIDTH (&window->info);
  window->input_rect.bottom = GST_VIDEO_INFO_HEIGHT (&window->info);

  /* Step 4: Decide render color space and set it on converter/processor */

  /* check HDR10 metadata. If HDR APIs are available, BT2020 primaries colorspcae
   * will be used.
   *
   * FIXME: need to query h/w level support. If that's not the case, tone-mapping
   * should be placed somewhere.
   *
   * FIXME: Non-HDR colorspace with BT2020 primaries will break rendering.
   * https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/1175
   * To workaround it, BT709 colorspace will be chosen for non-HDR case.
   */
#if (DXGI_HEADER_VERSION >= 5)
  {
    GstVideoMasteringDisplayInfo minfo;
    GstVideoContentLightLevel cll;

    if (gst_video_mastering_display_info_from_caps (&minfo, caps) &&
        gst_video_content_light_level_from_caps (&cll, caps)) {
      IDXGISwapChain4 *swapchain4 = NULL;
      HRESULT hr;

      hr = window->swap_chain->QueryInterface (IID_IDXGISwapChain4,
        (void **) &swapchain4);
      if (gst_d3d11_result (hr, window->device)) {
        GST_DEBUG_OBJECT (window, "Have HDR metadata, set to DXGI swapchain");

        gst_d3d11_hdr_meta_data_to_dxgi (&minfo, &cll, &hdr10_metadata);

        hr = swapchain4->SetHDRMetaData (DXGI_HDR_METADATA_TYPE_HDR10,
            sizeof (DXGI_HDR_METADATA_HDR10), &hdr10_metadata);
        if (!gst_d3d11_result (hr, window->device)) {
          GST_WARNING_OBJECT (window, "Couldn't set HDR metadata, hr 0x%x",
              (guint) hr);
        } else {
          have_hdr10 = TRUE;
        }

        swapchain4->Release ();
      }
    }
  }
#endif

  /* Step 5: Choose display color space */
  gst_video_info_set_format (&window->render_info,
      chosen_format->gst_format, display_width, display_height);

  /* preserve upstream colorimetry */
  window->render_info.colorimetry.primaries =
      window->info.colorimetry.primaries;
  window->render_info.colorimetry.transfer = window->info.colorimetry.transfer;
  /* prefer FULL range RGB. STUDIO range doesn't seem to be well supported
   * color space by GPUs and we don't need to preserve color range for
   * target display color space type */
  window->render_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

#if (DXGI_HEADER_VERSION >= 4)
  {
    IDXGISwapChain3 *swapchain3 = NULL;
    HRESULT hr;

    hr = window->swap_chain->QueryInterface (IID_IDXGISwapChain3,
        (void **) &swapchain3);

    if (gst_d3d11_result (hr, window->device)) {
      chosen_colorspace = gst_d3d11_find_swap_chain_color_space (&window->render_info,
          swapchain3, have_hdr10);
      if (chosen_colorspace) {
        native_colorspace_type =
            (DXGI_COLOR_SPACE_TYPE) chosen_colorspace->dxgi_color_space_type;
        hr = swapchain3->SetColorSpace1 (native_colorspace_type);
        if (!gst_d3d11_result (hr, window->device)) {
          GST_WARNING_OBJECT (window, "Failed to set colorspace %d, hr: 0x%x",
            native_colorspace_type, (guint) hr);
          chosen_colorspace = NULL;
          native_colorspace_type = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        } else {
          GST_DEBUG_OBJECT (window,
              "Set colorspace %d", native_colorspace_type);

          /* update with selected display color space */
          window->render_info.colorimetry.primaries =
              chosen_colorspace->primaries;
          window->render_info.colorimetry.transfer =
              chosen_colorspace->transfer;
          window->render_info.colorimetry.range =
              chosen_colorspace->range;
          window->render_info.colorimetry.matrix =
              chosen_colorspace->matrix;
        }
      }

      swapchain3->Release ();
    }
  }
#endif

  /* otherwise, use most common DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
   * color space */
  if (!chosen_colorspace) {
    GST_DEBUG_OBJECT (window, "No selected render color space, use BT709");
    window->render_info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
    window->render_info.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
    window->render_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
  }

#if (DXGI_HEADER_VERSION >= 4)
  if (chosen_colorspace) {
    const GstDxgiColorSpace *in_color_space =
        gst_d3d11_video_info_to_dxgi_color_space (&window->info);
    const GstD3D11Format *in_format =
        gst_d3d11_device_format_from_gst (window->device,
        GST_VIDEO_INFO_FORMAT (&window->info));
    gboolean hardware = FALSE;
    GstD3D11VideoProcessor *processor = NULL;

    if (in_color_space && in_format &&
        in_format->dxgi_format != DXGI_FORMAT_UNKNOWN) {
      g_object_get (window->device, "hardware", &hardware, NULL);
    }

    if (hardware) {
      processor =
          gst_d3d11_video_processor_new (window->device,
          GST_VIDEO_INFO_WIDTH (&window->info),
          GST_VIDEO_INFO_HEIGHT (&window->info),
          display_width, display_height);
    }

    if (processor) {
      DXGI_FORMAT in_dxgi_format = in_format->dxgi_format;
      DXGI_FORMAT out_dxgi_format = chosen_format->dxgi_format;
      DXGI_COLOR_SPACE_TYPE in_dxgi_color_space =
          (DXGI_COLOR_SPACE_TYPE) in_color_space->dxgi_color_space_type;
      DXGI_COLOR_SPACE_TYPE out_dxgi_color_space = native_colorspace_type;

      if (!gst_d3d11_video_processor_check_format_conversion (processor,
              in_dxgi_format, in_dxgi_color_space, out_dxgi_format,
              out_dxgi_color_space)) {
        GST_DEBUG_OBJECT (window, "Conversion is not supported by device");
        gst_d3d11_video_processor_free (processor);
        processor = NULL;
      } else {
        GST_DEBUG_OBJECT (window, "video processor supports conversion");
        gst_d3d11_video_processor_set_input_dxgi_color_space (processor,
            in_dxgi_color_space);
        gst_d3d11_video_processor_set_output_dxgi_color_space (processor,
            out_dxgi_color_space);

#if (DXGI_HEADER_VERSION >= 5)
        if (have_hdr10) {
          GST_DEBUG_OBJECT (window, "Set HDR metadata on video processor");
          gst_d3d11_video_processor_set_input_hdr10_metadata (processor,
              &hdr10_metadata);
          gst_d3d11_video_processor_set_output_hdr10_metadata (processor,
              &hdr10_metadata);
        }
#endif
      }

      window->processor = processor;
    }
  }
#endif
  *video_processor_available = !!window->processor;

  /* configure shader even if video processor is available for fallback */
  window->converter =
      gst_d3d11_color_converter_new (window->device, &window->info,
      &window->render_info);

  if (!window->converter) {
    GST_ERROR_OBJECT (window, "Cannot create converter");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create converter");
    goto error;
  }

  window->compositor =
      gst_d3d11_overlay_compositor_new (window->device, &window->render_info);
  if (!window->compositor) {
    GST_ERROR_OBJECT (window, "Cannot create overlay compositor");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create overlay compositor");
    goto error;
  }

#ifdef HAVE_DIRECT_WRITE
  if (window->render_stats)
    gst_d3d11_window_prepare_dwrite_device (window);
#endif
  gst_d3d11_device_unlock (window->device);

  /* call resize to allocated resources */
  klass->on_resize (window, display_width, display_height);

  if (window->requested_fullscreen != window->fullscreen) {
    klass->change_fullscreen_mode (window);
  }

  GST_DEBUG_OBJECT (window, "New swap chain 0x%p created", window->swap_chain);

  return TRUE;

error:
  gst_d3d11_device_unlock (window->device);

  return FALSE;
}

void
gst_d3d11_window_show (GstD3D11Window * window)
{
  GstD3D11WindowClass *klass;

  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  klass = GST_D3D11_WINDOW_GET_CLASS (window);

  if (klass->show)
    klass->show (window);
}

void
gst_d3d11_window_set_render_rectangle (GstD3D11Window * window, gint x, gint y,
    gint width, gint height)
{
  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  /* TODO: resize window and view */
}

static gboolean
gst_d3d11_window_buffer_ensure_processor_input (GstD3D11Window * self,
    GstBuffer * buffer, ID3D11VideoProcessorInputView ** in_view)
{
  GstD3D11Memory *mem;

  if (!self->processor)
    return FALSE;

  if (gst_buffer_n_memory (buffer) != 1)
    return FALSE;

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, 0);

  if (!gst_d3d11_video_processor_check_bind_flags_for_input_view
      (mem->desc.BindFlags)) {
    return FALSE;
  }

  if (!gst_d3d11_video_processor_ensure_input_view (self->processor, mem)) {
    GST_LOG_OBJECT (self, "Failed to create processor input view");
    return FALSE;
  }

  *in_view = mem->processor_input_view;

  return TRUE;
}

#ifdef HAVE_DIRECT_WRITE
static void
gst_d3d11_window_present_d2d (GstD3D11Window * self, GstStructure * stats)
{
  GstD3D11WindowPrivate *priv = self->priv;
  HRESULT hr;
  gdouble framerate = 0.0;
  guint64 dropped = 0;
  guint64 rendered = 0;
  std::wostringstream stats_str;
  ComPtr<IDWriteTextLayout> layout;
  FLOAT left;
  FLOAT top;

  if (!priv->d2d_device)
    return;

  if (!stats)
    return;

  gst_structure_get_double (stats, "average-rate", &framerate);
  gst_structure_get_uint64 (stats, "dropped", &dropped);
  gst_structure_get_uint64 (stats, "rendered", &rendered);

  stats_str.precision (5);
  stats_str << "Average-rate: " << framerate << std::endl;
  stats_str << "Dropped: " << dropped << std::endl;
  stats_str << "Rendered: " << rendered << std::endl;

  hr = priv->dwrite_factory->CreateTextLayout (stats_str.str().c_str(),
      (UINT32) stats_str.str().size(), priv->dwrite_format,
      self->render_rect.right - self->render_rect.left,
      self->render_rect.bottom - self->render_rect.top,
      &layout);
  if (!gst_d3d11_result (hr, self->device))
    return;

  left = self->render_rect.left + 5.0f;
  top = self->render_rect.top + 5.0f;

  priv->d2d_device_context->BeginDraw ();
  priv->d2d_device_context->DrawTextLayout(D2D1::Point2F(left, top),
      layout.Get(), priv->d2d_brush);

  hr = priv->d2d_device_context->EndDraw ();
  gst_d3d11_result (hr, self->device);

  return;
}
#endif

static GstFlowReturn
gst_d3d111_window_present (GstD3D11Window * self, GstBuffer * buffer,
    GstStructure * stats)
{
  GstD3D11WindowClass *klass = GST_D3D11_WINDOW_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_OK;
  guint present_flags = 0;

  if (buffer) {
    gst_buffer_replace (&self->cached_buffer, buffer);
  }

  if (self->cached_buffer) {
    ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES];
    ID3D11VideoProcessorInputView *piv = NULL;
    guint i, j, k;

    if (!gst_d3d11_window_buffer_ensure_processor_input (self,
        self->cached_buffer, &piv)) {
      for (i = 0, j = 0; i < gst_buffer_n_memory (self->cached_buffer); i++) {
        GstD3D11Memory *mem =
            (GstD3D11Memory *) gst_buffer_peek_memory (self->cached_buffer, i);
        for (k = 0; k < mem->num_shader_resource_views; k++) {
          srv[j] = mem->shader_resource_view[k];
          j++;
        }
      }
    }

    if (self->first_present) {
      D3D11_VIEWPORT viewport;

      viewport.TopLeftX = self->render_rect.left;
      viewport.TopLeftY = self->render_rect.top;
      viewport.Width = self->render_rect.right - self->render_rect.left;
      viewport.Height = self->render_rect.bottom - self->render_rect.top;
      viewport.MinDepth = 0.0f;
      viewport.MaxDepth = 1.0f;
      gst_d3d11_color_converter_update_viewport (self->converter,
          &viewport);
      gst_d3d11_overlay_compositor_update_viewport (self->compositor,
          &viewport);
    }

    if (self->processor && piv && self->pov) {
      if (!gst_d3d11_video_processor_render_unlocked (self->processor,
          &self->input_rect, piv, &self->render_rect, self->pov)) {
        GST_ERROR_OBJECT (self, "Couldn't render to backbuffer using processor");
        return GST_FLOW_ERROR;
      } else {
        GST_TRACE_OBJECT (self, "Rendered using processor");
      }
    } else {
      if (!gst_d3d11_color_converter_convert_unlocked (self->converter,
          srv, &self->rtv, NULL, NULL)) {
        GST_ERROR_OBJECT (self, "Couldn't render to backbuffer using converter");
        return GST_FLOW_ERROR;
      } else {
        GST_TRACE_OBJECT (self, "Rendered using converter");
      }
    }

    gst_d3d11_overlay_compositor_upload (self->compositor, self->cached_buffer);
    gst_d3d11_overlay_compositor_draw_unlocked (self->compositor, &self->rtv);

#if (DXGI_HEADER_VERSION >= 5)
    if (self->allow_tearing && self->fullscreen) {
      present_flags |= DXGI_PRESENT_ALLOW_TEARING;
    }
#endif

#if HAVE_DIRECT_WRITE
    gst_d3d11_window_present_d2d (self, stats);
#endif

    ret = klass->present (self, present_flags);

    self->first_present = FALSE;
  }

  return ret;
}

GstFlowReturn
gst_d3d11_window_render (GstD3D11Window * window, GstBuffer * buffer,
    GstVideoRectangle * rect, GstStructure * stats)
{
  GstMemory *mem;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), GST_FLOW_ERROR);
  g_return_val_if_fail (rect != NULL, GST_FLOW_ERROR);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_ERROR_OBJECT (window, "Invalid buffer");

    if (stats)
      gst_structure_free (stats);

    return GST_FLOW_ERROR;
  }

  gst_d3d11_device_lock (window->device);
  ret = gst_d3d111_window_present (window, buffer, stats);
  gst_d3d11_device_unlock (window->device);

  if (stats)
    gst_structure_free (stats);

  return ret;
}

gboolean
gst_d3d11_window_unlock (GstD3D11Window * window)
{
  GstD3D11WindowClass *klass;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);

  klass = GST_D3D11_WINDOW_GET_CLASS (window);

  if (klass->unlock)
    ret = klass->unlock (window);

  return ret;
}

gboolean
gst_d3d11_window_unlock_stop (GstD3D11Window * window)
{
  GstD3D11WindowClass *klass;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);

  klass = GST_D3D11_WINDOW_GET_CLASS (window);

  if (klass->unlock_stop)
    ret = klass->unlock_stop (window);

  gst_d3d11_device_lock (window->device);
  gst_clear_buffer (&window->cached_buffer);
  gst_d3d11_device_unlock (window->device);

  return ret;
}

void
gst_d3d11_window_unprepare (GstD3D11Window * window)
{
  GstD3D11WindowClass *klass;

  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  klass = GST_D3D11_WINDOW_GET_CLASS (window);

  if (klass->unprepare)
    klass->unprepare (window);
}

GstD3D11WindowNativeType
gst_d3d11_window_get_native_type_from_handle (guintptr handle)
{
  if (!handle)
    return GST_D3D11_WINDOW_NATIVE_TYPE_NONE;

#if (!GST_D3D11_WINAPI_ONLY_APP)
  if (IsWindow ((HWND) handle))
    return GST_D3D11_WINDOW_NATIVE_TYPE_HWND;
#else
  {
    ComPtr<IInspectable> window = reinterpret_cast<IInspectable*> (handle);
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> core_window;
    ComPtr<ABI::Windows::UI::Xaml::Controls::ISwapChainPanel> panel;

    if (SUCCEEDED (window.As (&core_window)))
      return GST_D3D11_WINDOW_NATIVE_TYPE_CORE_WINDOW;

    if (SUCCEEDED (window.As (&panel)))
      return GST_D3D11_WINDOW_NATIVE_TYPE_SWAP_CHAIN_PANEL;
  }
#endif

  return GST_D3D11_WINDOW_NATIVE_TYPE_NONE;
}

const gchar *
gst_d3d11_window_get_native_type_to_string (GstD3D11WindowNativeType type)
{
  switch (type) {
    case GST_D3D11_WINDOW_NATIVE_TYPE_NONE:
      return "none";
    case GST_D3D11_WINDOW_NATIVE_TYPE_HWND:
      return "hwnd";
    case GST_D3D11_WINDOW_NATIVE_TYPE_CORE_WINDOW:
      return "core-window";
    case GST_D3D11_WINDOW_NATIVE_TYPE_SWAP_CHAIN_PANEL:
      return "swap-chain-panel";
    default:
      break;
  }

  return "none";
}