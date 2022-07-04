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
#include "gstd3d11pluginutils.h"

#if GST_D3D11_WINAPI_APP
/* workaround for GetCurrentTime collision */
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <windows.ui.xaml.h>
#include <windows.applicationmodel.core.h>
#endif

#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_window_debug);
#define GST_CAT_DEFAULT gst_d3d11_window_debug


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
  static gsize mode_type = 0;

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
    GstBuffer * buffer, GstBuffer * render_target);
static void gst_d3d11_window_on_resize_default (GstD3D11Window * window,
    guint width, guint height);
static gboolean gst_d3d11_window_prepare_default (GstD3D11Window * window,
    guint display_width, guint display_height, GstCaps * caps, GError ** error);

static void
gst_d3d11_window_class_init (GstD3D11WindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_window_set_property;
  gobject_class->get_property = gst_d3d11_window_get_property;
  gobject_class->dispose = gst_d3d11_window_dispose;

  klass->on_resize = GST_DEBUG_FUNCPTR (gst_d3d11_window_on_resize_default);
  klass->prepare = GST_DEBUG_FUNCPTR (gst_d3d11_window_prepare_default);

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
  self->render_stats = DEFAULT_RENDER_STATS;
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
gst_d3d11_window_dispose (GObject * object)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  gst_clear_buffer (&self->backbuffer);
  GST_D3D11_CLEAR_COM (self->swap_chain);

  gst_clear_object (&self->compositor);
  gst_clear_object (&self->converter);

  gst_clear_buffer (&self->cached_buffer);
  gst_clear_object (&self->device);
  gst_clear_object (&self->allocator);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_window_on_resize_default (GstD3D11Window * self, guint width,
    guint height)
{
  GstD3D11Device *device = self->device;
  HRESULT hr;
  D3D11_TEXTURE2D_DESC desc;
  DXGI_SWAP_CHAIN_DESC swap_desc;
  ComPtr < ID3D11Texture2D > backbuffer;
  GstVideoRectangle src_rect, dst_rect, rst_rect;
  IDXGISwapChain *swap_chain;
  GstMemory *mem;
  GstD3D11Memory *dmem;
  ID3D11RenderTargetView *rtv;
  gsize size;

  gst_d3d11_device_lock (device);

  gst_clear_buffer (&self->backbuffer);
  if (!self->swap_chain)
    goto done;

  swap_chain = self->swap_chain;
  swap_chain->GetDesc (&swap_desc);
  hr = swap_chain->ResizeBuffers (0, width, height, self->dxgi_format,
      swap_desc.Flags);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't resize buffers, hr: 0x%x", (guint) hr);
    goto done;
  }

  hr = swap_chain->GetBuffer (0, IID_PPV_ARGS (&backbuffer));
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self,
        "Cannot get backbuffer from swapchain, hr: 0x%x", (guint) hr);
    goto done;
  }

  backbuffer->GetDesc (&desc);
  size = desc.Width * desc.Height;
  /* flip mode swapchain supports only 4 formats, rgba/bgra/rgb10a2/rgba64.
   * The size passed in alloc_wrapped() is not important here, since we never
   * try mapping this for CPU access */
  if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
    size *= 8;
  } else {
    size *= 4;
  }

  mem = gst_d3d11_allocator_alloc_wrapped (self->allocator,
      self->device, backbuffer.Get (), size, nullptr, nullptr);
  if (!mem) {
    GST_ERROR_OBJECT (self, "Couldn't allocate wrapped memory");
    goto done;
  }

  dmem = GST_D3D11_MEMORY_CAST (mem);
  rtv = gst_d3d11_memory_get_render_target_view (dmem, 0);
  if (!rtv) {
    GST_ERROR_OBJECT (self, "RTV is unavailable");
    gst_memory_unref (mem);
    goto done;
  }

  self->backbuffer = gst_buffer_new ();
  gst_buffer_append_memory (self->backbuffer, mem);

  self->surface_width = desc.Width;
  self->surface_height = desc.Height;

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.w = self->surface_width;
  dst_rect.h = self->surface_height;

  if (self->force_aspect_ratio) {
    src_rect.x = 0;
    src_rect.y = 0;

    switch (self->method) {
      case GST_VIDEO_ORIENTATION_90R:
      case GST_VIDEO_ORIENTATION_90L:
      case GST_VIDEO_ORIENTATION_UL_LR:
      case GST_VIDEO_ORIENTATION_UR_LL:
        src_rect.w = GST_VIDEO_INFO_HEIGHT (&self->render_info);
        src_rect.h = GST_VIDEO_INFO_WIDTH (&self->render_info);
        break;
      default:
        src_rect.w = GST_VIDEO_INFO_WIDTH (&self->render_info);
        src_rect.h = GST_VIDEO_INFO_HEIGHT (&self->render_info);
        break;
    }

    gst_video_sink_center_rect (src_rect, dst_rect, &rst_rect, TRUE);
  } else {
    rst_rect = dst_rect;
  }

  self->render_rect.left = rst_rect.x;
  self->render_rect.top = rst_rect.y;
  self->render_rect.right = rst_rect.x + rst_rect.w;
  self->render_rect.bottom = rst_rect.y + rst_rect.h;

  GST_LOG_OBJECT (self,
      "New client area %dx%d, render rect x: %d, y: %d, %dx%d",
      desc.Width, desc.Height, rst_rect.x, rst_rect.y, rst_rect.w, rst_rect.h);

  self->first_present = TRUE;

  /* redraw the last scene if cached buffer exits */
  if (self->cached_buffer)
    gst_d3d111_window_present (self, self->cached_buffer, self->backbuffer);

done:
  gst_d3d11_device_unlock (device);
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
    guint display_height, GstCaps * caps, GError ** error)
{
  GstD3D11WindowClass *klass;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);

  klass = GST_D3D11_WINDOW_GET_CLASS (window);
  g_assert (klass->prepare != NULL);

  GST_DEBUG_OBJECT (window, "Prepare window, display resolution %dx%d, caps %"
      GST_PTR_FORMAT, display_width, display_height, caps);

  return klass->prepare (window, display_width, display_height, caps, error);
}

static gboolean
gst_d3d11_window_prepare_default (GstD3D11Window * window, guint display_width,
    guint display_height, GstCaps * caps, GError ** error)
{
  GstD3D11Device *device = window->device;
  GstD3D11WindowClass *klass;
  guint swapchain_flags = 0;
  ID3D11Device *device_handle;
  guint num_supported_format = 0;
  HRESULT hr;
  UINT display_flags =
      D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DISPLAY;
  UINT supported_flags = 0;
  GstD3D11WindowDisplayFormat formats[] = {
    {DXGI_FORMAT_R8G8B8A8_UNORM, GST_VIDEO_FORMAT_RGBA, FALSE},
    {DXGI_FORMAT_B8G8R8A8_UNORM, GST_VIDEO_FORMAT_BGRA, FALSE},
    {DXGI_FORMAT_R10G10B10A2_UNORM, GST_VIDEO_FORMAT_RGB10A2_LE, FALSE},
  };
  const GstD3D11WindowDisplayFormat *chosen_format = NULL;
  GstDxgiColorSpace swapchain_colorspace;
  gboolean found_swapchain_colorspace = FALSE;
  gboolean hdr10_aware = FALSE;
  gboolean have_hdr10_meta = FALSE;
  DXGI_COLOR_SPACE_TYPE native_colorspace_type =
      DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  ComPtr < IDXGIFactory5 > factory5;
  IDXGIFactory1 *factory_handle;
  BOOL allow_tearing = FALSE;
  GstVideoMasteringDisplayInfo mdcv;
  GstVideoContentLightLevel cll;
  ComPtr < IDXGISwapChain3 > swapchain3;
  GstStructure *s;
  const gchar *cll_str = nullptr;
  const gchar *mdcv_str = nullptr;

  if (!window->allocator) {
    window->allocator =
        (GstD3D11Allocator *) gst_allocator_find (GST_D3D11_MEMORY_NAME);
    if (!window->allocator) {
      GST_ERROR_OBJECT (window, "Allocator is unavailable");
      return FALSE;
    }
  }

  /* Step 1: Clear old resources and objects */
  gst_clear_buffer (&window->cached_buffer);
  gst_clear_object (&window->compositor);
  gst_clear_object (&window->converter);

  /* Step 2: Decide display color format
   * If upstream format is 10bits, try DXGI_FORMAT_R10G10B10A2_UNORM first
   * Otherwise, use DXGI_FORMAT_B8G8R8A8_UNORM or DXGI_FORMAT_B8G8R8A8_UNORM
   */
  gst_video_info_from_caps (&window->info, caps);
  device_handle = gst_d3d11_device_get_device_handle (device);
  for (guint i = 0; i < G_N_ELEMENTS (formats); i++) {
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

  for (guint i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (&window->info); i++) {
    if (GST_VIDEO_INFO_COMP_DEPTH (&window->info, i) > 8) {
      if (formats[2].supported) {
        chosen_format = &formats[2];
      }
      break;
    }
  }

  if (!chosen_format) {
    /* prefer native format over conversion */
    for (guint i = 0; i < 2; i++) {
      if (formats[i].supported &&
          formats[i].gst_format == GST_VIDEO_INFO_FORMAT (&window->info)) {
        chosen_format = &formats[i];
        break;
      }
    }

    /* choose any color space then */
    if (!chosen_format) {
      for (guint i = 0; i < G_N_ELEMENTS (formats); i++) {
        if (formats[i].supported) {
          chosen_format = &formats[i];
          break;
        }
      }
    }
  }

  g_assert (chosen_format != nullptr);

  GST_DEBUG_OBJECT (window, "chosen render format %s (DXGI_FORMAT %d)",
      gst_video_format_to_string (chosen_format->gst_format),
      chosen_format->dxgi_format);

  /* Step 3: Create swapchain
   * (or reuse old swapchain if the format is not changed) */
  window->allow_tearing = FALSE;

  factory_handle = gst_d3d11_device_get_dxgi_factory_handle (device);
  hr = factory_handle->QueryInterface (IID_PPV_ARGS (&factory5));
  if (SUCCEEDED (hr)) {
    hr = factory5->CheckFeatureSupport (DXGI_FEATURE_PRESENT_ALLOW_TEARING,
        (void *) &allow_tearing, sizeof (allow_tearing));
  }

  if (SUCCEEDED (hr) && allow_tearing)
    window->allow_tearing = allow_tearing;

  gst_d3d11_device_lock (device);
  window->dxgi_format = chosen_format->dxgi_format;

  klass = GST_D3D11_WINDOW_GET_CLASS (window);
  if (!window->swap_chain &&
      !klass->create_swap_chain (window, window->dxgi_format,
          display_width, display_height, swapchain_flags,
          &window->swap_chain)) {
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

  window->prev_input_rect = window->input_rect;

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

  s = gst_caps_get_structure (caps, 0);
  mdcv_str = gst_structure_get_string (s, "mastering-display-info");
  cll_str = gst_structure_get_string (s, "content-light-level");
  if (mdcv_str && cll_str &&
      gst_video_mastering_display_info_from_string (&mdcv, mdcv_str) &&
      gst_video_content_light_level_from_string (&cll, cll_str)) {
    have_hdr10_meta = TRUE;
  }

  hr = window->swap_chain->QueryInterface (IID_PPV_ARGS (&swapchain3));
  if (gst_d3d11_result (hr, device)) {
    found_swapchain_colorspace =
        gst_d3d11_find_swap_chain_color_space (&window->render_info,
        swapchain3.Get (), &swapchain_colorspace);
    if (found_swapchain_colorspace) {
      native_colorspace_type =
          (DXGI_COLOR_SPACE_TYPE) swapchain_colorspace.dxgi_color_space_type;
      hr = swapchain3->SetColorSpace1 (native_colorspace_type);
      if (!gst_d3d11_result (hr, window->device)) {
        GST_WARNING_OBJECT (window, "Failed to set colorspace %d, hr: 0x%x",
            native_colorspace_type, (guint) hr);
        found_swapchain_colorspace = FALSE;
        native_colorspace_type = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      } else {
        ComPtr < IDXGISwapChain4 > swapchain4;

        GST_DEBUG_OBJECT (window, "Set colorspace %d", native_colorspace_type);

        /* update with selected display color space */
        window->render_info.colorimetry.primaries =
            swapchain_colorspace.primaries;
        window->render_info.colorimetry.transfer =
            swapchain_colorspace.transfer;
        window->render_info.colorimetry.range = swapchain_colorspace.range;
        window->render_info.colorimetry.matrix = swapchain_colorspace.matrix;

        /* DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12, undefined in old
         * mingw header */
        if (native_colorspace_type == 12 && have_hdr10_meta) {
          hr = swapchain3.As (&swapchain4);
          if (gst_d3d11_result (hr, device)) {
            DXGI_HDR_METADATA_HDR10 hdr10_metadata = { 0, };

            GST_DEBUG_OBJECT (window,
                "Have HDR metadata, set to DXGI swapchain");

            gst_d3d11_hdr_meta_data_to_dxgi (&mdcv, &cll, &hdr10_metadata);

            hr = swapchain4->SetHDRMetaData (DXGI_HDR_METADATA_TYPE_HDR10,
                sizeof (DXGI_HDR_METADATA_HDR10), &hdr10_metadata);
            if (!gst_d3d11_result (hr, device)) {
              GST_WARNING_OBJECT (window,
                  "Couldn't set HDR metadata, hr 0x%x", (guint) hr);
            } else {
              hdr10_aware = TRUE;
            }
          }
        }
      }
    }
  }

  /* otherwise, use most common DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
   * color space */
  if (!found_swapchain_colorspace) {
    GST_DEBUG_OBJECT (window, "No selected render color space, use BT709");
    window->render_info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
    window->render_info.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
    window->render_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    window->render_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
  }

  window->converter = gst_d3d11_converter_new (device,
      &window->info, &window->render_info, nullptr);

  if (!window->converter) {
    GST_ERROR_OBJECT (window, "Cannot create converter");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create converter");
    goto error;
  }

  if (have_hdr10_meta) {
    g_object_set (window->converter, "src-mastering-display-info", mdcv_str,
        "src-content-light-level", cll_str, nullptr);
    if (hdr10_aware) {
      g_object_set (window->converter, "dest-mastering-display-info", mdcv_str,
          "dest-content-light-level", cll_str, nullptr);
    }
  }

  window->compositor =
      gst_d3d11_overlay_compositor_new (window->device, &window->render_info);
  if (!window->compositor) {
    GST_ERROR_OBJECT (window, "Cannot create overlay compositor");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create overlay compositor");
    goto error;
  }
  gst_d3d11_device_unlock (window->device);

  /* call resize to allocated resources */
  klass->on_resize (window, display_width, display_height);

  if (window->requested_fullscreen != window->fullscreen)
    klass->change_fullscreen_mode (window);

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
gst_d3d11_window_set_render_rectangle (GstD3D11Window * window,
    const GstVideoRectangle * rect)
{
  GstD3D11WindowClass *klass;

  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  klass = GST_D3D11_WINDOW_GET_CLASS (window);

  if (klass->set_render_rectangle)
    klass->set_render_rectangle (window, rect);
}

void
gst_d3d11_window_set_title (GstD3D11Window * window, const gchar * title)
{
  GstD3D11WindowClass *klass;

  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  klass = GST_D3D11_WINDOW_GET_CLASS (window);

  if (klass->set_title)
    klass->set_title (window, title);
}

static GstFlowReturn
gst_d3d111_window_present (GstD3D11Window * self, GstBuffer * buffer,
    GstBuffer * backbuffer)
{
  GstD3D11WindowClass *klass = GST_D3D11_WINDOW_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_OK;
  guint present_flags = 0;
  GstVideoCropMeta *crop_meta;
  RECT input_rect = self->input_rect;
  RECT *prev_rect = &self->prev_input_rect;
  ID3D11RenderTargetView *rtv;
  GstMemory *mem;
  GstD3D11Memory *dmem;

  if (!buffer)
    return GST_FLOW_OK;

  if (!backbuffer) {
    GST_ERROR_OBJECT (self, "Empty render target");
    return GST_FLOW_ERROR;
  }

  mem = gst_buffer_peek_memory (backbuffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_ERROR_OBJECT (self, "Invalid back buffer");
    return GST_FLOW_ERROR;
  }

  dmem = GST_D3D11_MEMORY_CAST (mem);
  rtv = gst_d3d11_memory_get_render_target_view (dmem, 0);
  if (!rtv) {
    GST_ERROR_OBJECT (self, "RTV is unavailable");
    return GST_FLOW_ERROR;
  }

  crop_meta = gst_buffer_get_video_crop_meta (buffer);
  if (crop_meta) {
    input_rect.left = crop_meta->x;
    input_rect.right = crop_meta->x + crop_meta->width;
    input_rect.top = crop_meta->y;
    input_rect.bottom = crop_meta->y + crop_meta->height;
  }

  if (input_rect.left != prev_rect->left || input_rect.top != prev_rect->top ||
      input_rect.right != prev_rect->right ||
      input_rect.bottom != prev_rect->bottom) {
    g_object_set (self->converter, "src-x", (gint) input_rect.left,
        "src-y", (gint) input_rect.top,
        "src-width", (gint) (input_rect.right - input_rect.left),
        "src-height", (gint) (input_rect.bottom - input_rect.top), nullptr);

    self->prev_input_rect = input_rect;
  }

  if (self->first_present) {
    D3D11_VIEWPORT viewport;

    viewport.TopLeftX = self->render_rect.left;
    viewport.TopLeftY = self->render_rect.top;
    viewport.Width = self->render_rect.right - self->render_rect.left;
    viewport.Height = self->render_rect.bottom - self->render_rect.top;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    g_object_set (self->converter, "dest-x", (gint) self->render_rect.left,
        "dest-y", (gint) self->render_rect.top,
        "dest-width",
        (gint) (self->render_rect.right - self->render_rect.left),
        "dest-height",
        (gint) (self->render_rect.bottom - self->render_rect.top),
        "video-direction", self->method, nullptr);
    gst_d3d11_overlay_compositor_update_viewport (self->compositor, &viewport);
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (self->converter,
          buffer, backbuffer)) {
    GST_ERROR_OBJECT (self, "Couldn't render buffer");
    return GST_FLOW_ERROR;
  }

  gst_d3d11_overlay_compositor_upload (self->compositor, buffer);
  gst_d3d11_overlay_compositor_draw_unlocked (self->compositor, &rtv);

  if (self->allow_tearing && self->fullscreen)
    present_flags |= DXGI_PRESENT_ALLOW_TEARING;

  if (klass->present)
    ret = klass->present (self, present_flags);

  self->first_present = FALSE;

  return ret;
}

GstFlowReturn
gst_d3d11_window_render (GstD3D11Window * window, GstBuffer * buffer)
{
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), GST_FLOW_ERROR);

  gst_d3d11_device_lock (window->device);
  if (buffer)
    gst_buffer_replace (&window->cached_buffer, buffer);

  ret = gst_d3d111_window_present (window, window->cached_buffer,
      window->backbuffer);
  gst_d3d11_device_unlock (window->device);

  return ret;
}

GstFlowReturn
gst_d3d11_window_render_on_shared_handle (GstD3D11Window * window,
    GstBuffer * buffer, HANDLE shared_handle, guint texture_misc_flags,
    guint64 acquire_key, guint64 release_key)
{
  GstD3D11WindowClass *klass;
  GstFlowReturn ret = GST_FLOW_OK;
  GstD3D11WindowSharedHandleData data = { nullptr, };

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), GST_FLOW_ERROR);

  klass = GST_D3D11_WINDOW_GET_CLASS (window);

  g_assert (klass->open_shared_handle != NULL);
  g_assert (klass->release_shared_handle != NULL);

  data.shared_handle = shared_handle;
  data.texture_misc_flags = texture_misc_flags;
  data.acquire_key = acquire_key;
  data.release_key = release_key;

  gst_d3d11_device_lock (window->device);
  if (!klass->open_shared_handle (window, &data)) {
    GST_ERROR_OBJECT (window, "Couldn't open shared handle");
    gst_d3d11_device_unlock (window->device);
    return GST_FLOW_OK;
  }

  ret = gst_d3d111_window_present (window, buffer, data.render_target);

  klass->release_shared_handle (window, &data);
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
#endif
#if GST_D3D11_WINAPI_ONLY_APP
  {
    /* *INDENT-OFF* */
    ComPtr<IInspectable> window = reinterpret_cast<IInspectable*> (handle);
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> core_window;
    ComPtr<ABI::Windows::UI::Xaml::Controls::ISwapChainPanel> panel;
    /* *INDENT-ON* */

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

void
gst_d3d11_window_set_orientation (GstD3D11Window * window,
    GstVideoOrientationMethod method)
{
  if (method == GST_VIDEO_ORIENTATION_AUTO ||
      method == GST_VIDEO_ORIENTATION_CUSTOM) {
    return;
  }

  gst_d3d11_device_lock (window->device);
  if (window->method != method) {
    window->method = method;
    if (window->swap_chain) {
      GstD3D11WindowClass *klass = GST_D3D11_WINDOW_GET_CLASS (window);

      klass->on_resize (window, window->surface_width, window->surface_height);
    }
  }
  gst_d3d11_device_unlock (window->device);
}
