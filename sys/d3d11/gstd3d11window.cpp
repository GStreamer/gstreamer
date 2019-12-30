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

static void
gst_d3d11_window_class_init (GstD3D11WindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_window_set_property;
  gobject_class->get_property = gst_d3d11_window_get_property;
  gobject_class->dispose = gst_d3d11_window_dispose;

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

  self->aspect_ratio_n = 1;
  self->aspect_ratio_d = 1;
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

void
gst_d3d11_window_on_resize (GstD3D11Window * window, guint width, guint height)
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

  /* Set zero width and height here. dxgi will decide client area by itself */
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

  width = window->width;
  height = window->height;

  {
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.w = width * window->aspect_ratio_n;
    src_rect.h = height * window->aspect_ratio_d;

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = window->surface_width;
    dst_rect.h = window->surface_height;

    if (window->force_aspect_ratio) {
      src_rect.w = width * window->aspect_ratio_n;
      src_rect.h = height * window->aspect_ratio_d;

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

#if (DXGI_HEADER_VERSION >= 5)
static inline UINT16
fraction_to_uint (guint num, guint den, guint scale)
{
  gdouble val;
  gst_util_fraction_to_double (num, den, &val);

  return (UINT16) val *scale;
}

static void
mastering_display_gst_to_dxgi (GstVideoMasteringDisplayInfo * m,
    GstVideoContentLightLevel * c, DXGI_HDR_METADATA_HDR10 * meta)
{
  meta->RedPrimary[0] = fraction_to_uint (m->Rx_n, m->Rx_d, 50000);
  meta->RedPrimary[1] = fraction_to_uint (m->Ry_n, m->Ry_d, 50000);
  meta->GreenPrimary[0] = fraction_to_uint (m->Gx_n, m->Gx_d, 50000);
  meta->GreenPrimary[1] = fraction_to_uint (m->Gy_n, m->Gy_d, 50000);
  meta->BluePrimary[0] = fraction_to_uint (m->Bx_n, m->Bx_d, 50000);
  meta->BluePrimary[1] = fraction_to_uint (m->By_n, m->By_d, 50000);
  meta->WhitePoint[0] = fraction_to_uint (m->Wx_n, m->Wx_d, 50000);
  meta->WhitePoint[1] = fraction_to_uint (m->Wy_n, m->Wy_d, 50000);
  meta->MaxMasteringLuminance =
      fraction_to_uint (m->max_luma_n, m->max_luma_d, 1);
  meta->MinMasteringLuminance =
      fraction_to_uint (m->min_luma_n, m->min_luma_d, 1);
  meta->MaxContentLightLevel = fraction_to_uint (c->maxCLL_n, c->maxCLL_d, 1);
  meta->MaxFrameAverageLightLevel =
      fraction_to_uint (c->maxFALL_n, c->maxFALL_d, 1);
}

/* missing in mingw header... */
typedef enum
{
  GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 = 0,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 = 1,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709 = 2,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020 = 3,
  GST_DXGI_COLOR_SPACE_RESERVED = 4,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601 = 5,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601 = 6,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 = 7,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 = 8,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 = 9,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020 = 10,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 = 11,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020 = 13,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 = 14,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020 = 15,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020 = 16,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 = 17,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020 = 18,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 = 19,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709 = 20,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020 = 21,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709 = 22,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020 = 23,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020 = 24,
  GST_DXGI_COLOR_SPACE_CUSTOM = 0xFFFFFFFF
} GST_DXGI_COLOR_SPACE_TYPE;

typedef struct
{
  GST_DXGI_COLOR_SPACE_TYPE type;
  GstVideoColorRange range;
  GstVideoTransferFunction transfer;
  GstVideoColorPrimaries primaries;
} DxgiColorSpaceMap;

/* https://docs.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type */
static const DxgiColorSpaceMap colorspace_map[] = {
  /* RGB, bt709 */
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_BT709, GST_VIDEO_COLOR_PRIMARIES_BT709},
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_GAMMA10, GST_VIDEO_COLOR_PRIMARIES_BT709},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_BT709, GST_VIDEO_COLOR_PRIMARIES_BT709},
  /* RGB, bt2020 */
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_BT2020_10, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_BT2020_10, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  /* RGB, bt2084 */
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_SMPTE2084, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020,
        GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_SMPTE2084, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  /* RGB, SRGB */
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_SRGB, GST_VIDEO_COLOR_PRIMARIES_BT709},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_SRGB, GST_VIDEO_COLOR_PRIMARIES_BT2020},
};

static gboolean
gst_d3d11_window_color_space_from_video_info (GstD3D11Window * self,
    GstVideoInfo * info, IDXGISwapChain4 * swapchain,
    GST_DXGI_COLOR_SPACE_TYPE * dxgi_colorspace)
{
  guint i;
  gint best_idx = -1;
  gint best_score = 0;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (dxgi_colorspace != NULL, FALSE);

  /* We render only RGB for now */
  if (!GST_VIDEO_FORMAT_INFO_IS_RGB (info->finfo))
    return FALSE;

  /* find the best matching colorspace */
  for (i = 0; i < G_N_ELEMENTS (colorspace_map); i++) {
    GstVideoColorimetry *cinfo = &info->colorimetry;
    UINT can_support = 0;
    HRESULT hr;
    gint score = 0;
    GstVideoTransferFunction transfer = cinfo->transfer;
    DXGI_COLOR_SPACE_TYPE type = (DXGI_COLOR_SPACE_TYPE) colorspace_map[i].type;

    if (transfer == GST_VIDEO_TRANSFER_BT2020_12)
      transfer = GST_VIDEO_TRANSFER_BT2020_10;

    hr = swapchain->CheckColorSpaceSupport (type, &can_support);

    if (SUCCEEDED (hr) &&
        (can_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ==
        DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) {
      if (cinfo->range == colorspace_map[i].range)
        score++;

      if (transfer == colorspace_map[i].transfer)
        score++;

      if (cinfo->primaries == colorspace_map[i].primaries)
        score++;

      GST_DEBUG_OBJECT (self,
          "colorspace %d supported, score %d", type, score);

      if (score > best_score) {
        best_score = score;
        best_idx = i;
      }
    } else {
      GST_DEBUG_OBJECT (self,
          "colorspace %d not supported", type);
    }
  }

  if (best_idx < 0)
    return FALSE;

  *dxgi_colorspace = colorspace_map[best_idx].type;

  return TRUE;
}
#endif

gboolean
gst_d3d11_window_prepare (GstD3D11Window * window, guint width, guint height,
    guint aspect_ratio_n, guint aspect_ratio_d, GstCaps * caps, GError ** error)
{
  GstD3D11WindowClass *klass;
  GstCaps *render_caps;
  guint swapchain_flags = 0;
#if (DXGI_HEADER_VERSION >= 5)
  gboolean have_cll = FALSE;
  gboolean have_mastering = FALSE;
  gboolean swapchain4_available = FALSE;
#endif

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);
  g_return_val_if_fail (aspect_ratio_n > 0, FALSE);
  g_return_val_if_fail (aspect_ratio_d > 0, FALSE);

  GST_DEBUG_OBJECT (window, "Prepare window with %dx%d caps %" GST_PTR_FORMAT,
      width, height, caps);

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
      gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (&window->render_info));
  if (!window->render_format ||
      window->render_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (window, "Unknown dxgi render format");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Unknown dxgi render format");

    return FALSE;
  }

  gst_video_info_from_caps (&window->info, caps);

  if (window->converter)
    gst_d3d11_color_converter_free (window->converter);
  window->converter = NULL;

  if (window->compositor)
    gst_d3d11_overlay_compositor_free (window->compositor);
  window->compositor = NULL;

  /* preserve upstream colorimetry */
  window->render_info.width = width;
  window->render_info.height = height;

  window->render_info.colorimetry.primaries =
      window->info.colorimetry.primaries;
  window->render_info.colorimetry.transfer = window->info.colorimetry.transfer;

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
#if (DXGI_HEADER_VERSION >= 5)
  if (!gst_video_content_light_level_from_caps (&window->content_light_level,
          caps)) {
    gst_video_content_light_level_init (&window->content_light_level);
  } else {
    have_cll = TRUE;
  }

  if (!gst_video_mastering_display_info_from_caps
      (&window->mastering_display_info, caps)) {
    gst_video_mastering_display_info_init (&window->mastering_display_info);
  } else {
    have_mastering = TRUE;
  }

  if (gst_d3d11_device_get_chosen_dxgi_factory_version (window->device) >=
      GST_D3D11_DXGI_FACTORY_5) {
    GST_DEBUG_OBJECT (window, "DXGI 1.5 interface is available");
    swapchain4_available = TRUE;

    g_object_get (window->device,
        "allow-tearing", &window->allow_tearing, NULL);
    if (window->allow_tearing) {
      GST_DEBUG_OBJECT (window, "device support tearning");
      swapchain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
  }
#endif

  if (window->swap_chain) {
    gst_d3d11_device_lock (window->device);
    gst_d3d11_window_release_resources (window->device, window);
    gst_d3d11_device_unlock (window->device);
  }

  window->aspect_ratio_n = aspect_ratio_n;
  window->aspect_ratio_d = aspect_ratio_d;

  window->render_rect.left = 0;
  window->render_rect.top = 0;
  window->render_rect.right = width;
  window->render_rect.bottom = height;

  window->width = width;
  window->height = height;

  klass = GST_D3D11_WINDOW_GET_CLASS (window);
  if (!klass->create_swap_chain (window, window->render_format->dxgi_format,
          width, height, swapchain_flags, &window->swap_chain)) {
    GST_ERROR_OBJECT (window, "Cannot create swapchain");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create swapchain");

    return FALSE;
  }
#if (DXGI_HEADER_VERSION >= 5)
  if (swapchain4_available) {
    HRESULT hr;
    GST_DXGI_COLOR_SPACE_TYPE ctype;
    IDXGISwapChain4* swap_chain4 = (IDXGISwapChain4 *) window->swap_chain;

    if (gst_d3d11_window_color_space_from_video_info (window,
            &window->render_info, swap_chain4, &ctype)) {
      hr = swap_chain4->SetColorSpace1 ((DXGI_COLOR_SPACE_TYPE) ctype);

      if (!gst_d3d11_result (hr, window->device)) {
        GST_WARNING_OBJECT (window, "Failed to set colorspace %d, hr: 0x%x",
            ctype, (guint) hr);
      } else {
        GST_DEBUG_OBJECT (window, "Set colorspace %d", ctype);
      }

      if (have_cll && have_mastering) {
        DXGI_HDR_METADATA_HDR10 metadata = { 0, };

        GST_DEBUG_OBJECT (window, "Have HDR metadata, set to DXGI swapchain");

        mastering_display_gst_to_dxgi (&window->mastering_display_info,
            &window->content_light_level, &metadata);

        hr = swap_chain4->SetHDRMetaData (DXGI_HDR_METADATA_TYPE_HDR10,
            sizeof (DXGI_HDR_METADATA_HDR10), &metadata);
        if (!gst_d3d11_result (hr, window->device)) {
          GST_WARNING_OBJECT (window, "Couldn't set HDR metadata, hr 0x%x",
              (guint) hr);
        }
      }
    } else {
      GST_DEBUG_OBJECT (window,
          "Could not get color space from %" GST_PTR_FORMAT, caps);
    }
  }
#endif

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
    guint i, j, k;

    for (i = 0, j = 0; i < gst_buffer_n_memory (self->cached_buffer); i++) {
      GstD3D11Memory *mem =
          (GstD3D11Memory *) gst_buffer_peek_memory (self->cached_buffer, i);
      for (k = 0; k < mem->num_shader_resource_views; k++) {
        srv[j] = mem->shader_resource_view[k];
        j++;
      }
    }

    if (self->first_present) {
      gst_d3d11_color_converter_update_rect (self->converter,
          &self->render_rect);
      gst_d3d11_overlay_compositor_update_rect (self->compositor,
          &self->render_rect);
    }

    gst_d3d11_color_converter_convert_unlocked (self->converter,
        srv, &self->rtv);

    gst_d3d11_overlay_compositor_upload (self->compositor, self->cached_buffer);
    gst_d3d11_overlay_compositor_draw_unlocked (self->compositor, &self->rtv);

#if (DXGI_HEADER_VERSION >= 5)
    if (self->allow_tearing) {
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