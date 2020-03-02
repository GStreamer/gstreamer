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
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

using namespace Microsoft::WRL;
#endif

extern "C" {
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_window_debug);
#define GST_CAT_DEFAULT gst_d3d11_window_debug
}

enum
{
  PROP_0,
  PROP_D3D11_DEVICE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS,
  PROP_FULLSCREEN_TOGGLE_MODE,
  PROP_FULLSCREEN,
  PROP_WINDOW_HANDLE,
};

#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE
#define DEFAULT_FULLSCREEN_TOGGLE_MODE    GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE
#define DEFAULT_FULLSCREEN                FALSE

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
G_DEFINE_ABSTRACT_TYPE (GstD3D11Window, gst_d3d11_window, GST_TYPE_OBJECT);

static void gst_d3d11_window_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_window_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_window_dispose (GObject * object);
static GstFlowReturn gst_d3d111_window_present (GstD3D11Window * self,
    GstBuffer * buffer);
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

static void
gst_d3d11_window_release_resources (GstD3D11Device * device,
    GstD3D11Window * window)
{
  if (window->rtv) {
    window->rtv->Release ();
    window->rtv = NULL;
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

  if (self->converter) {
    gst_d3d11_color_converter_free (self->converter);
    self->converter = NULL;
  }

  if (self->compositor) {
    gst_d3d11_overlay_compositor_free (self->compositor);
    self->compositor = NULL;
  }

  gst_clear_buffer (&self->cached_buffer);
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_window_on_resize_default (GstD3D11Window * window, guint width,
    guint height)
{
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

  swap_chain->GetDesc (&swap_desc);
  hr = swap_chain->ResizeBuffers (0, width, height, DXGI_FORMAT_UNKNOWN,
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

  window->first_present = TRUE;

  /* redraw the last scene if cached buffer exits */
  gst_d3d111_window_present (window, NULL);

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

gboolean
gst_d3d11_window_prepare (GstD3D11Window * window, guint display_width,
    guint display_height, GstCaps * caps, gboolean * video_processor_available,
    GError ** error)
{
  GstD3D11WindowClass *klass;
  GstCaps *render_caps;
  guint swapchain_flags = 0;
  gboolean need_processor_output_configure = FALSE;
  gboolean need_processor_input_configure = FALSE;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);

  GST_DEBUG_OBJECT (window, "Prepare window, display resolution %dx%d, caps %"
      GST_PTR_FORMAT, display_width, display_height, caps);

  gst_clear_buffer (&window->cached_buffer);

  render_caps = gst_d3d11_device_get_supported_caps (window->device,
      (D3D11_FORMAT_SUPPORT) (D3D11_FORMAT_SUPPORT_TEXTURE2D |
          D3D11_FORMAT_SUPPORT_DISPLAY));

  GST_DEBUG_OBJECT (window, "rendering caps %" GST_PTR_FORMAT, render_caps);
  render_caps = gst_d3d11_caps_fixate_format (caps, render_caps);

  if (!render_caps || gst_caps_is_empty (render_caps)) {
    GST_ERROR_OBJECT (window, "Couldn't define render caps");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Couldn't define render caps");
    gst_clear_caps (&render_caps);

    return FALSE;
  }

  render_caps = gst_caps_fixate (render_caps);
  gst_video_info_from_caps (&window->render_info, render_caps);
  gst_clear_caps (&render_caps);

  window->render_format =
      gst_d3d11_device_format_from_gst (window->device,
      GST_VIDEO_INFO_FORMAT (&window->render_info));
  if (!window->render_format ||
      window->render_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (window, "Unknown dxgi render format");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Unknown dxgi render format");

    return FALSE;
  }

  gst_video_info_from_caps (&window->info, caps);

  if (window->processor)
    gst_d3d11_video_processor_free (window->processor);
  window->processor = NULL;

  if (window->converter)
    gst_d3d11_color_converter_free (window->converter);
  window->converter = NULL;

  if (window->compositor)
    gst_d3d11_overlay_compositor_free (window->compositor);
  window->compositor = NULL;

  /* preserve upstream colorimetry */
  window->render_info.width = display_width;
  window->render_info.height = display_height;

  window->render_info.colorimetry.primaries =
      window->info.colorimetry.primaries;
  window->render_info.colorimetry.transfer = window->info.colorimetry.transfer;

  window->processor =
      gst_d3d11_video_processor_new (window->device,
      GST_VIDEO_INFO_WIDTH (&window->info),
      GST_VIDEO_INFO_HEIGHT (&window->info),
      display_width, display_height);
  if (window->processor) {
    const GstD3D11Format *in_format;
    const GstD3D11Format *out_format;
    gboolean input_support = FALSE;
    gboolean out_support = FALSE;

    in_format = gst_d3d11_device_format_from_gst (window->device,
        GST_VIDEO_INFO_FORMAT (&window->info));
    out_format = gst_d3d11_device_format_from_gst (window->device,
        GST_VIDEO_INFO_FORMAT (&window->render_info));

    if (gst_d3d11_video_processor_supports_input_format (window->processor,
            in_format->dxgi_format)) {
      input_support = TRUE;
    } else {
      GST_DEBUG_OBJECT (window,
          "IVideoProcessor cannot support input dxgi format %d",
          in_format->dxgi_format);
    }

    if (gst_d3d11_video_processor_supports_output_format (window->processor,
            out_format->dxgi_format)) {
      out_support = TRUE;
    } else {
      GST_DEBUG_OBJECT (window,
          "IVideoProcessor cannot support output dxgi format %d",
          out_format->dxgi_format);
    }

    if (!input_support || !out_support) {
      gst_d3d11_video_processor_free (window->processor);
      window->processor = NULL;
    } else {
      GST_DEBUG_OBJECT (window, "IVideoProcessor interface available");
      *video_processor_available = TRUE;
      need_processor_input_configure = TRUE;
      need_processor_output_configure = TRUE;
    }
  }

  /* configure shader even if video processor is available for fallback */
  window->converter =
      gst_d3d11_color_converter_new (window->device, &window->info,
      &window->render_info);

  if (!window->converter) {
    GST_ERROR_OBJECT (window, "Cannot create converter");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create converter");

    return FALSE;
  }

  window->compositor =
      gst_d3d11_overlay_compositor_new (window->device, &window->render_info);
  if (!window->compositor) {
    GST_ERROR_OBJECT (window, "Cannot create overlay compositor");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create overlay compositor");

    return FALSE;
  }

  window->allow_tearing = FALSE;
  g_object_get (window->device, "allow-tearing", &window->allow_tearing, NULL);
  if (window->allow_tearing) {
    GST_DEBUG_OBJECT (window, "device support tearning");
    swapchain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
  }

  /* release swapchain if render format is being changed */
  gst_d3d11_device_lock (window->device);
  if (window->swap_chain &&
      window->render_format->dxgi_format != window->dxgi_format) {
    gst_d3d11_window_release_resources (window->device, window);
  }

  window->dxgi_format = window->render_format->dxgi_format;

  window->render_rect.left = 0;
  window->render_rect.top = 0;
  window->render_rect.right = display_width;
  window->render_rect.bottom = display_height;

  window->input_rect.left = 0;
  window->input_rect.top = 0;
  window->input_rect.right = GST_VIDEO_INFO_WIDTH (&window->info);
  window->input_rect.bottom = GST_VIDEO_INFO_HEIGHT (&window->info);

  klass = GST_D3D11_WINDOW_GET_CLASS (window);
  if (!window->swap_chain &&
      !klass->create_swap_chain (window, window->dxgi_format,
          display_width, display_height, swapchain_flags, &window->swap_chain)) {
    GST_ERROR_OBJECT (window, "Cannot create swapchain");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create swapchain");
    gst_d3d11_device_unlock (window->device);

    return FALSE;
  }
  gst_d3d11_device_unlock (window->device);

#if (DXGI_HEADER_VERSION >= 4)
  {
    IDXGISwapChain3 *swapchain3 = NULL;
    HRESULT hr;

    hr = window->swap_chain->QueryInterface (IID_IDXGISwapChain3,
        (void **) &swapchain3);

    if (gst_d3d11_result (hr, window->device)) {
      DXGI_COLOR_SPACE_TYPE ctype;

      if (gst_d3d11_find_swap_chain_color_space (&window->render_info,
              swapchain3, &ctype)) {
        hr = swapchain3->SetColorSpace1 (ctype);
        if (!gst_d3d11_result (hr, window->device)) {
          GST_WARNING_OBJECT (window, "Failed to set colorspace %d, hr: 0x%x",
            ctype, (guint) hr);
        } else {
          GST_DEBUG_OBJECT (window, "Set colorspace %d", ctype);

          if (window->processor)
            need_processor_output_configure =
                !gst_d3d11_video_processor_set_output_dxgi_color_space
                (window->processor, ctype);
        }
      }

      swapchain3->Release ();
    }
  }

  if (window->processor) {
    if (need_processor_output_configure) {
      /* Set most common color space */
      need_processor_output_configure =
          !gst_d3d11_video_processor_set_output_dxgi_color_space
          (window->processor, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }

    if (need_processor_input_configure) {
      DXGI_COLOR_SPACE_TYPE ctype;
      gst_d3d11_video_info_to_dxgi_color_space (&window->info, &ctype);
      need_processor_input_configure =
          !gst_d3d11_video_processor_set_input_dxgi_color_space
          (window->processor, ctype);
    }
  }
#endif

  if (window->processor && need_processor_output_configure) {
    gst_d3d11_video_processor_set_output_color_space (window->processor,
        &window->render_info.colorimetry);
  }

  if (window->processor && need_processor_input_configure) {
    gst_d3d11_video_processor_set_input_color_space (window->processor,
        &window->info.colorimetry);
  }

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
        DXGI_HDR_METADATA_HDR10 metadata = { 0, };

        GST_DEBUG_OBJECT (window, "Have HDR metadata, set to DXGI swapchain");

        gst_d3d11_hdr_meta_data_to_dxgi (&minfo, &cll, &metadata);

        hr = swapchain4->SetHDRMetaData (DXGI_HDR_METADATA_TYPE_HDR10,
            sizeof (DXGI_HDR_METADATA_HDR10), &metadata);
        if (!gst_d3d11_result (hr, window->device)) {
          GST_WARNING_OBJECT (window, "Couldn't set HDR metadata, hr 0x%x",
              (guint) hr);
        } else if (window->processor) {
          gst_d3d11_video_processor_set_input_hdr10_metadata (window->processor,
              &metadata);
          gst_d3d11_video_processor_set_output_hdr10_metadata (window->processor,
              &metadata);
        }

        swapchain4->Release ();
      }
    }
  }
#endif

  /* call resize to allocated resources */
  klass->on_resize (window, display_width, display_height);

  if (window->requested_fullscreen != window->fullscreen) {
    klass->change_fullscreen_mode (window);
  }

  GST_DEBUG_OBJECT (window, "New swap chain 0x%p created", window->swap_chain);

  return TRUE;
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
  ID3D11VideoProcessorInputView *view;
  GQuark quark;

  if (!self->processor)
    return FALSE;

  if (gst_buffer_n_memory (buffer) != 1)
    return FALSE;

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, 0);

  if (!gst_d3d11_video_processor_check_bind_flags_for_input_view
      (mem->desc.BindFlags)) {
    return FALSE;
  }

  quark = gst_d3d11_video_processor_input_view_quark ();
  view = (ID3D11VideoProcessorInputView *)
      gst_mini_object_get_qdata (GST_MINI_OBJECT (mem), quark);

  if (!view) {
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC in_desc;

    in_desc.FourCC = 0;
    in_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    in_desc.Texture2D.MipSlice = 0;
    in_desc.Texture2D.ArraySlice = mem->subresource_index;

    GST_TRACE_OBJECT (self, "Create new processor input view");

    if (!gst_d3d11_video_processor_create_input_view (self->processor,
         &in_desc, mem->texture, &view)) {
      GST_LOG_OBJECT (self, "Failed to create processor input view");
      return FALSE;
    }

    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem), quark, view,
        (GDestroyNotify) gst_d3d11_video_processor_input_view_release);
  } else {
    GST_TRACE_OBJECT (self, "Reuse existing processor input view %p", view);
  }

  *in_view = view;

  return TRUE;
}

static GstFlowReturn
gst_d3d111_window_present (GstD3D11Window * self, GstBuffer * buffer)
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
      gst_d3d11_color_converter_update_rect (self->converter,
          &self->render_rect);
      gst_d3d11_overlay_compositor_update_rect (self->compositor,
          &self->render_rect);
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
          srv, &self->rtv)) {
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

    ret = klass->present (self, present_flags);

    self->first_present = FALSE;
  }

  return ret;
}

GstFlowReturn
gst_d3d11_window_render (GstD3D11Window * window, GstBuffer * buffer,
    GstVideoRectangle * rect)
{
  GstMemory *mem;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), GST_FLOW_ERROR);
  g_return_val_if_fail (rect != NULL, GST_FLOW_ERROR);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_ERROR_OBJECT (window, "Invalid buffer");

    return GST_FLOW_ERROR;
  }

  gst_d3d11_device_lock (window->device);
  ret = gst_d3d111_window_present (window, buffer);
  gst_d3d11_device_unlock (window->device);

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