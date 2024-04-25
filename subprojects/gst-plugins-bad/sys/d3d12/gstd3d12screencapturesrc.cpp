/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

/**
 * SECTION:element-d3d12screencapturesrc
 * @title: d3d12screencapturesrc
 *
 * Direct3D12 screen capture element
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 d3d12screencapturesrc ! queue ! d3d12videosink
 * ```
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12screencapturesrc.h"
#include "gstd3d12dxgicapture.h"
#ifdef HAVE_WGC
#include "gstd3d12graphicscapture.h"
#endif
#include "gstd3d12pluginutils.h"
#include <mutex>
#include <wrl.h>
#include <string.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY (gst_d3d12_screen_capture_debug);
#define GST_CAT_DEFAULT gst_d3d12_screen_capture_debug

enum
{
  PROP_0,
  PROP_MONITOR_INDEX,
  PROP_MONITOR_HANDLE,
  PROP_SHOW_CURSOR,
  PROP_CROP_X,
  PROP_CROP_Y,
  PROP_CROP_WIDTH,
  PROP_CROP_HEIGHT,
  PROP_WINDOW_HANDLE,
  PROP_SHOW_BORDER,
  PROP_CAPTURE_API,
  PROP_ADAPTER,
  PROP_WINDOW_CAPTURE_MODE,
};

enum GstD3D12ScreenCaptureAPI
{
  GST_D3D12_SCREEN_CAPTURE_API_DXGI,
  GST_D3D12_SCREEN_CAPTURE_API_WGC,
};

enum GstD3D12WindowCaptureMode
{
  GST_D3D12_WINDOW_CAPTURE_DEFAULT,
  GST_D3D12_WINDOW_CAPTURE_CLIENT,
};

#ifdef HAVE_WGC
/**
 * GstD3D11ScreenCaptureAPI:
 *
 * Since: 1.26
 */
#define GST_TYPE_D3D12_SCREEN_CAPTURE_API (gst_d3d12_screen_capture_api_get_type())
static GType
gst_d3d12_screen_capture_api_get_type (void)
{
  static GType api_type = 0;

  GST_D3D12_CALL_ONCE_BEGIN {
    static const GEnumValue api_types[] = {
      /**
       * GstD3D12ScreenCaptureAPI::dxgi:
       *
       * Since: 1.26
       */
      {GST_D3D12_SCREEN_CAPTURE_API_DXGI, "DXGI Desktop Duplication", "dxgi"},

      /**
       * GstD3D12ScreenCaptureAPI::wgc:
       *
       * Since: 1.26
       */
      {GST_D3D12_SCREEN_CAPTURE_API_WGC, "Windows Graphics Capture", "wgc"},
      {0, nullptr, nullptr},
    };

    api_type = g_enum_register_static ("GstD3D12ScreenCaptureAPI", api_types);
  } GST_D3D12_CALL_ONCE_END;

  return api_type;
}

/**
 * GstD3D12WindowCaptureMode:
 *
 * Since: 1.26
 */
#define GST_TYPE_D3D12_WINDOW_CAPTURE_MODE (gst_d3d11_window_capture_mode_get_type())
static GType
gst_d3d11_window_capture_mode_get_type (void)
{
  static GType type = 0;

  GST_D3D12_CALL_ONCE_BEGIN {
    static const GEnumValue hwnd_modes[] = {
      /**
       * GstD3D12WindowCaptureMode::default:
       *
       * Since: 1.26
       */
      {GST_D3D12_WINDOW_CAPTURE_DEFAULT,
          "Capture entire window area", "default"},

      /**
       * GstD3D12WindowCaptureMode::client:
       *
       * Since: 1.26
       */
      {GST_D3D12_WINDOW_CAPTURE_CLIENT, "Capture client area", "client"},
      {0, nullptr, nullptr},
    };

    type = g_enum_register_static ("GstD3D12WindowCaptureMode", hwnd_modes);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}
#endif

#define DEFAULT_MONITOR_INDEX -1
#define DEFAULT_SHOW_CURSOR FALSE
#define DEFAULT_SHOW_BORDER FALSE
#define DEFAULT_CAPTURE_API GST_D3D12_SCREEN_CAPTURE_API_DXGI
#define DEFAULT_ADAPTER -1
#define DEFAULT_WINDOW_CAPTURE_MODE GST_D3D12_WINDOW_CAPTURE_DEFAULT

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, "BGRA")
        ", pixel-aspect-ratio = 1/1, colorimetry = (string) sRGB; "
        GST_VIDEO_CAPS_MAKE ("BGRA") ", pixel-aspect-ratio = 1/1, "
        "colorimetry = (string) sRGB"));

struct GstD3D12ScreenCaptureSrcPrivate
{
  ~GstD3D12ScreenCaptureSrcPrivate ()
  {
    if (pool) {
      gst_buffer_pool_set_active (pool, FALSE);
      gst_clear_object (&pool);
    }

    gst_clear_object (&capture);
  }

  guint64 last_frame_no = 0;
  GstClockID clock_id = nullptr;
  GstVideoInfo video_info;

  GstD3D12ScreenCapture *capture = nullptr;

  GstBufferPool *pool = nullptr;

  gint64 adapter_luid = 0;
  gint adapter_index = DEFAULT_ADAPTER;
  gint monitor_index = DEFAULT_MONITOR_INDEX;
  HMONITOR monitor_handle = nullptr;
  HWND window_handle = nullptr;
  gboolean show_cursor = DEFAULT_SHOW_CURSOR;
  gboolean show_border = DEFAULT_SHOW_BORDER;

  CaptureCropRect crop_rect = { };
  D3D12_BOX crop_box = { };
  GstD3D12ScreenCaptureAPI capture_api = DEFAULT_CAPTURE_API;
  GstD3D12ScreenCaptureAPI selected_capture_api = DEFAULT_CAPTURE_API;
  GstD3D12WindowCaptureMode hwnd_capture_mode = DEFAULT_WINDOW_CAPTURE_MODE;

  gboolean flushing = FALSE;
  GstClockTime latency = GST_CLOCK_TIME_NONE;

  gboolean downstream_supports_d3d12 = FALSE;

  std::recursive_mutex lock;
  std::mutex flush_lock;
};

struct _GstD3D12ScreenCaptureSrc
{
  GstBaseSrc src;
  GstD3D12Device *device;
  GstD3D12ScreenCaptureSrcPrivate *priv;
};

static void gst_d3d12_screen_capture_src_finalize (GObject * object);
static void gst_d3d12_screen_capture_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d12_screen_capture_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstClock *gst_d3d12_screen_capture_src_provide_clock (GstElement *
    element);
static void gst_d3d12_screen_capture_src_set_context (GstElement * element,
    GstContext * context);

static GstCaps *gst_d3d12_screen_capture_src_get_caps (GstBaseSrc * bsrc,
    GstCaps * filter);
static GstCaps *gst_d3d12_screen_capture_src_fixate (GstBaseSrc * bsrc,
    GstCaps * caps);
static gboolean gst_d3d12_screen_capture_src_set_caps (GstBaseSrc * bsrc,
    GstCaps * caps);
static gboolean gst_d3d12_screen_capture_src_decide_allocation (GstBaseSrc *
    bsrc, GstQuery * query);
static gboolean gst_d3d12_screen_capture_src_start (GstBaseSrc * bsrc);
static gboolean gst_d3d12_screen_capture_src_stop (GstBaseSrc * bsrc);
static gboolean gst_d3d12_screen_capture_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_d3d12_screen_capture_src_unlock_stop (GstBaseSrc * bsrc);
static gboolean
gst_d3d12_screen_capture_src_src_query (GstBaseSrc * bsrc, GstQuery * query);

static GstFlowReturn gst_d3d12_screen_capture_src_create (GstBaseSrc * bsrc,
    guint64 offset, guint size, GstBuffer ** buf);

#define gst_d3d12_screen_capture_src_parent_class parent_class
G_DEFINE_TYPE (GstD3D12ScreenCaptureSrc, gst_d3d12_screen_capture_src,
    GST_TYPE_BASE_SRC);

static void
gst_d3d12_screen_capture_src_class_init (GstD3D12ScreenCaptureSrcClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto basesrc_class = GST_BASE_SRC_CLASS (klass);

  object_class->finalize = gst_d3d12_screen_capture_src_finalize;
  object_class->set_property = gst_d3d12_screen_capture_src_set_property;
  object_class->get_property = gst_d3d12_screen_capture_src_get_property;

  g_object_class_install_property (object_class, PROP_MONITOR_INDEX,
      g_param_spec_int ("monitor-index", "Monitor Index",
          "Zero-based index for monitor to capture (-1 = primary monitor)",
          -1, G_MAXINT, DEFAULT_MONITOR_INDEX,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_MONITOR_HANDLE,
      g_param_spec_uint64 ("monitor-handle", "Monitor Handle",
          "A HMONITOR handle of monitor to capture",
          0, G_MAXUINT64, 0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_SHOW_CURSOR,
      g_param_spec_boolean ("show-cursor",
          "Show Mouse Cursor", "Whether to show mouse cursor",
          DEFAULT_SHOW_CURSOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_CROP_X,
      g_param_spec_uint ("crop-x", "Crop X",
          "Horizontal coordinate of top left corner for the screen capture area",
          0, G_MAXUINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_CROP_Y,
      g_param_spec_uint ("crop-y", "Crop Y",
          "Vertical coordinate of top left corner for the screen capture area",
          0, G_MAXUINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_CROP_WIDTH,
      g_param_spec_uint ("crop-width", "Crop Width",
          "Width of screen capture area (0 = maximum)",
          0, G_MAXUINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_CROP_HEIGHT,
      g_param_spec_uint ("crop-height", "Crop Height",
          "Height of screen capture area (0 = maximum)",
          0, G_MAXUINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

#ifdef HAVE_WGC
  if (gst_d3d12_graphics_capture_load_library ()) {
    /**
     * GstD3D12ScreenCaptureSrc:window-handle:
     *
     * HWND window handle to capture
     *
     * Since: 1.26
     */
    g_object_class_install_property (object_class, PROP_WINDOW_HANDLE,
        g_param_spec_uint64 ("window-handle", "Window Handle",
            "A HWND handle of window to capture",
            0, G_MAXUINT64, 0,
            (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
                GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_STATIC_STRINGS)));

    /**
     * GstD3D12ScreenCaptureSrc:show-border:
     *
     * Show border lines to capture area when WGC mode is selected.
     * This feature requires Windows11 or newer
     *
     * Since: 1.26
     */
    g_object_class_install_property (object_class, PROP_SHOW_BORDER,
        g_param_spec_boolean ("show-border", "Show Border",
            "Show border lines to capture area when WGC mode is selected",
            DEFAULT_SHOW_BORDER,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_READWRITE
                | G_PARAM_STATIC_STRINGS)));

    /**
     * GstD3D12ScreenCaptureSrc:capture-api:
     *
     * Capture API to use
     *
     * Since: 1.26
     */
    g_object_class_install_property (object_class, PROP_CAPTURE_API,
        g_param_spec_enum ("capture-api", "Capture API", "Capture API to use",
            GST_TYPE_D3D12_SCREEN_CAPTURE_API,
            DEFAULT_CAPTURE_API,
            (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
                GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_STATIC_STRINGS)));
    gst_type_mark_as_plugin_api (GST_TYPE_D3D12_SCREEN_CAPTURE_API,
        (GstPluginAPIFlags) 0);

    /**
     * GstD3D12ScreenCaptureSrc:adapter:
     *
     * DXGI Adapter index for creating device when WGC mode is selected
     *
     * Since: 1.26
     */
    g_object_class_install_property (object_class, PROP_ADAPTER,
        g_param_spec_int ("adapter", "Adapter",
            "DXGI Adapter index for creating device when WGC mode is selected "
            "(-1 for default)",
            -1, G_MAXINT32, DEFAULT_ADAPTER,
            (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
                GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_STATIC_STRINGS)));

    /**
     * GstD3D12ScreenCaptureSrc:window-capture-mode:
     *
     * Window capture mode to use
     *
     * Since: 1.26
     */
    g_object_class_install_property (object_class, PROP_WINDOW_CAPTURE_MODE,
        g_param_spec_enum ("window-capture-mode", "Window Capture Mode",
            "Window capture mode to use if \"window-handle\" is set",
            GST_TYPE_D3D12_WINDOW_CAPTURE_MODE, DEFAULT_WINDOW_CAPTURE_MODE,
            (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
                GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_STATIC_STRINGS)));
    gst_type_mark_as_plugin_api (GST_TYPE_D3D12_WINDOW_CAPTURE_MODE,
        (GstPluginAPIFlags) 0);
  }
#endif

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_provide_clock);
  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Screen Capture Source", "Source/Video",
      "Captures desktop screen", "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &src_template);

  basesrc_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_get_caps);
  basesrc_class->fixate =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_fixate);
  basesrc_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_set_caps);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_decide_allocation);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_stop);
  basesrc_class->unlock =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_unlock);
  basesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_unlock_stop);
  basesrc_class->query =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_src_query);
  basesrc_class->create =
      GST_DEBUG_FUNCPTR (gst_d3d12_screen_capture_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_screen_capture_debug,
      "d3d12screencapturesrc", 0, "d3d12screencapturesrc");
}

static void
gst_d3d12_screen_capture_src_init (GstD3D12ScreenCaptureSrc * self)
{
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);

  self->priv = new GstD3D12ScreenCaptureSrcPrivate ();

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_d3d12_screen_capture_src_finalize (GObject * object)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (object);

  delete self->priv;
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_screen_capture_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_MONITOR_INDEX:
      priv->monitor_index = g_value_get_int (value);
      break;
    case PROP_MONITOR_HANDLE:
      priv->monitor_handle = (HMONITOR) g_value_get_uint64 (value);
      break;
    case PROP_SHOW_CURSOR:
      priv->show_cursor = g_value_get_boolean (value);
      break;
    case PROP_CROP_X:
      priv->crop_rect.crop_x = g_value_get_uint (value);
      break;
    case PROP_CROP_Y:
      priv->crop_rect.crop_y = g_value_get_uint (value);
      break;
    case PROP_CROP_WIDTH:
      priv->crop_rect.crop_w = g_value_get_uint (value);
      break;
    case PROP_CROP_HEIGHT:
      priv->crop_rect.crop_h = g_value_get_uint (value);
      break;
    case PROP_WINDOW_HANDLE:
      priv->window_handle = (HWND) g_value_get_uint64 (value);
      break;
    case PROP_SHOW_BORDER:
      priv->show_border = g_value_get_boolean (value);
#ifdef HAVE_WGC
      if (priv->capture &&
          priv->capture_api == GST_D3D12_SCREEN_CAPTURE_API_WGC) {
        auto wgc = GST_D3D12_GRAPHICS_CAPTURE (priv->capture);
        gst_d3d12_graphics_capture_show_border (wgc, priv->show_border);
      }
#endif
      break;
    case PROP_CAPTURE_API:
      priv->capture_api = (GstD3D12ScreenCaptureAPI) g_value_get_enum (value);
      break;
    case PROP_ADAPTER:
      priv->adapter_index = g_value_get_int (value);
      break;
    case PROP_WINDOW_CAPTURE_MODE:
      priv->hwnd_capture_mode =
          (GstD3D12WindowCaptureMode) g_value_get_enum (value);
#ifdef HAVE_WGC
      if (priv->capture &&
          priv->capture_api == GST_D3D12_SCREEN_CAPTURE_API_WGC) {
        auto wgc = GST_D3D12_GRAPHICS_CAPTURE (priv->capture);
        gst_d3d12_graphics_capture_set_client_only (wgc,
            priv->hwnd_capture_mode == GST_D3D12_WINDOW_CAPTURE_CLIENT);
      }
#endif
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static void
gst_d3d12_screen_capture_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_MONITOR_INDEX:
      g_value_set_int (value, priv->monitor_index);
      break;
    case PROP_MONITOR_HANDLE:
      g_value_set_uint64 (value, (guint64) priv->monitor_handle);
      break;
    case PROP_SHOW_CURSOR:
      g_value_set_boolean (value, priv->show_cursor);
      break;
    case PROP_CROP_X:
      g_value_set_uint (value, priv->crop_rect.crop_x);
      break;
    case PROP_CROP_Y:
      g_value_set_uint (value, priv->crop_rect.crop_y);
      break;
    case PROP_CROP_WIDTH:
      g_value_set_uint (value, priv->crop_rect.crop_w);
      break;
    case PROP_CROP_HEIGHT:
      g_value_set_uint (value, priv->crop_rect.crop_h);
      break;
    case PROP_WINDOW_HANDLE:
      g_value_set_uint64 (value, (guint64) priv->window_handle);
      break;
    case PROP_SHOW_BORDER:
      g_value_set_boolean (value, priv->show_border);
      break;
    case PROP_CAPTURE_API:
      g_value_set_enum (value, priv->capture_api);
      break;
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter_index);
      break;
    case PROP_WINDOW_CAPTURE_MODE:
      g_value_set_enum (value, priv->hwnd_capture_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static GstClock *
gst_d3d12_screen_capture_src_provide_clock (GstElement * element)
{
  return gst_system_clock_obtain ();
}

static void
gst_d3d12_screen_capture_src_set_context (GstElement * element,
    GstContext * context)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_d3d12_handle_set_context_for_adapter_luid (element,
        context, priv->adapter_luid, &self->device);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static void
gst_d3d12_screen_capture_src_get_crop_box (GstD3D12ScreenCaptureSrc * self,
    D3D12_BOX & box)
{
  auto priv = self->priv;
  guint screen_width, screen_height;

  box.front = 0;
  box.back = 1;

  gst_d3d12_screen_capture_get_size (priv->capture, &screen_width,
      &screen_height);

  if ((priv->crop_rect.crop_x + priv->crop_rect.crop_w) > screen_width ||
      (priv->crop_rect.crop_y + priv->crop_rect.crop_h) > screen_height) {
    GST_WARNING ("Capture region outside of the screen bounds; ignoring.");

    box.left = 0;
    box.top = 0;
    box.right = screen_width;
    box.bottom = screen_height;
  } else {
    box.left = priv->crop_rect.crop_x;
    box.top = priv->crop_rect.crop_y;
    box.right = priv->crop_rect.crop_w ?
        (priv->crop_rect.crop_x + priv->crop_rect.crop_w) : screen_width;
    box.bottom = priv->crop_rect.crop_h ?
        (priv->crop_rect.crop_y + priv->crop_rect.crop_h) : screen_height;
  }
}

static GstCaps *
gst_d3d12_screen_capture_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;
  guint width, height;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!priv->capture) {
    GST_DEBUG_OBJECT (self, "capture object is not configured yet");
    return gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  }

  if (priv->selected_capture_api == GST_D3D12_SCREEN_CAPTURE_API_WGC) {
    gst_d3d12_screen_capture_get_size (priv->capture, &width, &height);
  } else {
    gst_d3d12_screen_capture_src_get_crop_box (self, priv->crop_box);
    width = priv->crop_box.right - priv->crop_box.left;
    height = priv->crop_box.bottom - priv->crop_box.top;
  }

  auto caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  caps = gst_caps_make_writable (caps);

  gst_caps_set_simple (caps, "width", G_TYPE_INT, width, "height",
      G_TYPE_INT, height, nullptr);

  if (filter) {
    auto tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);

    caps = tmp;
  }

  return caps;
}

static GstCaps *
gst_d3d12_screen_capture_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstCaps *d3d12_caps = nullptr;

  caps = gst_caps_make_writable (caps);
  auto size = gst_caps_get_size (caps);

  for (guint i = 0; i < size; i++) {
    auto s = gst_caps_get_structure (caps, i);
    gst_structure_fixate_field_nearest_fraction (s, "framerate", 30, 1);

    if (!d3d12_caps) {
      auto features = gst_caps_get_features (caps, i);

      if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
        d3d12_caps = gst_caps_new_empty ();
        gst_caps_append_structure (d3d12_caps, gst_structure_copy (s));

        gst_caps_set_features (d3d12_caps, 0,
            gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY,
                nullptr));
        break;
      }
    }
  }

  if (d3d12_caps) {
    gst_caps_unref (caps);
    caps = d3d12_caps;
  }

  return gst_caps_fixate (caps);
}

static gboolean
gst_d3d12_screen_capture_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);

  auto features = gst_caps_get_features (caps, 0);
  if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
    priv->downstream_supports_d3d12 = TRUE;
  } else {
    priv->downstream_supports_d3d12 = FALSE;
  }

  gst_video_info_from_caps (&priv->video_info, caps);
  gst_base_src_set_blocksize (bsrc, GST_VIDEO_INFO_SIZE (&priv->video_info));

  return TRUE;
}

static gboolean
gst_d3d12_screen_capture_src_decide_allocation (GstBaseSrc * bsrc,
    GstQuery * query)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;
  GstBufferPool *pool = nullptr;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  GstVideoInfo vinfo;

  if (priv->pool) {
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_clear_object (&priv->pool);
  }

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_ERROR_OBJECT (self, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&vinfo, caps);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&vinfo);

    min = max = 0;
    update_pool = FALSE;
  }

  if (pool && priv->downstream_supports_d3d12) {
    if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
      gst_clear_object (&pool);
    } else {
      auto dpool = GST_D3D12_BUFFER_POOL (pool);
      if (!gst_d3d12_device_is_equal (dpool->device, self->device))
        gst_clear_object (&pool);
    }
  }

  if (!pool) {
    if (priv->downstream_supports_d3d12)
      pool = gst_d3d12_buffer_pool_new (self->device);
    else
      pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (priv->downstream_supports_d3d12) {
    D3D12_RESOURCE_FLAGS resource_flags =
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    auto params = gst_buffer_pool_config_get_d3d12_allocation_params (config);
    if (!params) {
      params = gst_d3d12_allocation_params_new (self->device, &vinfo,
          GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
          D3D12_HEAP_FLAG_SHARED);
    } else {
      gst_d3d12_allocation_params_set_resource_flags (params, resource_flags);
      gst_d3d12_allocation_params_unset_resource_flags (params,
          D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
      gst_d3d12_allocation_params_set_heap_flags (params,
          D3D12_HEAP_FLAG_SHARED);
    }

    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config");
    gst_clear_object (&pool);
    return FALSE;
  }

  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (!priv->downstream_supports_d3d12) {
    priv->pool = gst_d3d12_buffer_pool_new (self->device);

    config = gst_buffer_pool_get_config (priv->pool);

    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    D3D12_RESOURCE_FLAGS resource_flags =
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    auto params = gst_d3d12_allocation_params_new (self->device, &vinfo,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
        D3D12_HEAP_FLAG_SHARED);
    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);

    if (!gst_buffer_pool_set_config (priv->pool, config)) {
      GST_ERROR_OBJECT (self, "Failed to set config for internal pool");
      gst_clear_object (&priv->pool);
      gst_clear_object (&pool);
      return FALSE;
    }

    if (!gst_buffer_pool_set_active (priv->pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Failed to activate internal pool");
      gst_clear_object (&pool);
      return FALSE;
    }
  }

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_d3d12_screen_capture_src_start (GstBaseSrc * bsrc)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;
  GstFlowReturn ret;
  HMONITOR monitor = priv->monitor_handle;
  ComPtr < IDXGIAdapter1 > adapter;
  GstD3D12ScreenCapture *capture = nullptr;
  HRESULT hr;
  GstD3D12ScreenCaptureAPI requested_api;

  std::unique_lock < std::recursive_mutex > lk (priv->lock);
  requested_api = priv->capture_api;
  if (priv->window_handle) {
    priv->selected_capture_api = GST_D3D12_SCREEN_CAPTURE_API_WGC;
  } else {
    priv->selected_capture_api = requested_api;

    if (monitor) {
      hr = gst_d3d12_screen_capture_find_output_for_monitor (monitor,
          &adapter, nullptr);
    } else if (priv->monitor_index < 0) {
      hr = gst_d3d12_screen_capture_find_primary_monitor (&monitor,
          &adapter, nullptr);
    } else {
      hr = gst_d3d12_screen_capture_find_nth_monitor (priv->monitor_index,
          &monitor, &adapter, nullptr);
    }

    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't get monitor handle");
      goto error;
    }
  }

  if (priv->selected_capture_api == GST_D3D12_SCREEN_CAPTURE_API_DXGI) {
    DXGI_ADAPTER_DESC desc;
    adapter->GetDesc (&desc);

    priv->adapter_luid = gst_d3d12_luid_to_int64 (&desc.AdapterLuid);
    gst_clear_object (&self->device);

    gst_d3d12_ensure_element_data_for_adapter_luid (GST_ELEMENT_CAST (self),
        priv->adapter_luid, &self->device);
  } else {
    gst_clear_object (&self->device);
    gst_d3d12_ensure_element_data (GST_ELEMENT_CAST (self),
        priv->adapter_index, &self->device);
  }

  if (!self->device) {
    lk.unlock ();
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("D3D12 device is not available"), (nullptr));
    return FALSE;
  }
#ifdef HAVE_WGC
  if (priv->selected_capture_api == GST_D3D12_SCREEN_CAPTURE_API_WGC) {
    capture = gst_d3d12_graphics_capture_new (self->device,
        priv->window_handle, monitor);
  } else
#endif
  {
    capture = gst_d3d12_dxgi_capture_new (self->device, monitor);
  }

  if (!capture) {
    GST_ERROR_OBJECT (self, "Couldn't create capture object");
    goto error;
  }

  /* Check if we can open device */
  ret = gst_d3d12_screen_capture_prepare (capture);
  switch (ret) {
    case GST_D3D12_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR:
    case GST_FLOW_OK:
      break;
    case GST_D3D12_SCREEN_CAPTURE_FLOW_UNSUPPORTED:
#ifdef HAVE_WGC
      if (priv->selected_capture_api == GST_D3D12_SCREEN_CAPTURE_API_DXGI) {
        gst_clear_object (&capture);
        GST_WARNING_OBJECT (self, "DXGI capture is not available");
        capture = gst_d3d12_graphics_capture_new (self->device,
            nullptr, monitor);
        if (capture) {
          priv->selected_capture_api = GST_D3D12_SCREEN_CAPTURE_API_WGC;
          GST_INFO_OBJECT (self, "Fallback to Windows Graphics Capture");
          break;
        }
      }
#endif
      goto unsupported;
    default:
      goto error;
  }

  priv->last_frame_no = -1;
  priv->latency = GST_CLOCK_TIME_NONE;
  priv->capture = capture;

#ifdef HAVE_WGC
  if (priv->selected_capture_api == GST_D3D12_SCREEN_CAPTURE_API_WGC) {
    auto wgc = GST_D3D12_GRAPHICS_CAPTURE (priv->capture);
    gst_d3d12_graphics_capture_show_cursor (wgc, priv->show_cursor);
    gst_d3d12_graphics_capture_show_border (wgc, priv->show_border);
    gst_d3d12_graphics_capture_set_client_only (wgc,
        priv->hwnd_capture_mode == GST_D3D12_WINDOW_CAPTURE_CLIENT);
  }
#endif

  if (priv->capture_api != priv->selected_capture_api) {
    priv->capture_api = priv->selected_capture_api;
    lk.unlock ();
    g_object_notify (G_OBJECT (self), "capture-api");
  }

  return TRUE;

error:
  {
    gst_clear_object (&capture);
    lk.unlock ();
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to prepare capture object with given configuration, "
            "monitor-index: %d, monitor-handle: %p",
            priv->monitor_index, priv->monitor_handle), (nullptr));
    return FALSE;
  }

unsupported:
  {
    gst_clear_object (&capture);
    lk.unlock ();
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Failed to prepare capture object with given configuration, "
            "monitor-index: %d, monitor-handle: %p",
            priv->monitor_index, priv->monitor_handle),
        ("Try run the application on the integrated GPU"));
    return FALSE;
  }
}

static gboolean
gst_d3d12_screen_capture_src_stop (GstBaseSrc * bsrc)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;

  if (priv->pool) {
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_clear_object (&priv->pool);
  }

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  gst_clear_object (&priv->capture);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d12_screen_capture_src_unlock (GstBaseSrc * bsrc)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->flush_lock);
  if (priv->capture)
    gst_d3d12_screen_capture_unlock (priv->capture);

  if (priv->clock_id) {
    GST_DEBUG_OBJECT (self, "Waking up waiting clock");
    gst_clock_id_unschedule (priv->clock_id);
  }

  priv->flushing = TRUE;

  return TRUE;
}

static gboolean
gst_d3d12_screen_capture_src_unlock_stop (GstBaseSrc * bsrc)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->flush_lock);
  if (priv->capture)
    gst_d3d12_screen_capture_unlock_stop (priv->capture);

  priv->flushing = FALSE;

  return TRUE;
}

static gboolean
gst_d3d12_screen_capture_src_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      if (gst_d3d12_handle_context_query (GST_ELEMENT_CAST (self), query,
              self->device)) {
        return TRUE;
      }
      break;
    }
    case GST_QUERY_LATENCY:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      if (GST_CLOCK_TIME_IS_VALID (priv->latency)) {
        gst_query_set_latency (query, TRUE, priv->latency, GST_CLOCK_TIME_NONE);
        return TRUE;
      }
      break;
    }
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
}

#ifdef HAVE_WGC
static GstFlowReturn
gst_d3d12_screen_capture_src_wgc_capture (GstBaseSrc * bsrc,
    guint64 offset, guint size, GstBuffer ** buf)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;
  GstClockTime base_time;
  GstClockTime next_capture_ts;
  GstClockTime dur;
  guint64 next_frame_no;

  auto fps_n = GST_VIDEO_INFO_FPS_N (&priv->video_info);
  auto fps_d = GST_VIDEO_INFO_FPS_D (&priv->video_info);

  if (fps_n <= 0 || fps_d <= 0)
    return GST_FLOW_NOT_NEGOTIATED;

  auto clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
  {
    std::unique_lock < std::mutex > lk (priv->flush_lock);
    if (priv->flushing) {
      gst_clear_object (&clock);
      return GST_FLOW_FLUSHING;
    }

    base_time = GST_ELEMENT_CAST (self)->base_time;
    next_capture_ts = gst_clock_get_time (clock);
    next_capture_ts -= base_time;

    next_frame_no = gst_util_uint64_scale (next_capture_ts,
        fps_n, GST_SECOND * fps_d);

    if (next_frame_no == priv->last_frame_no) {
      GstClockID id;
      GstClockReturn clock_ret;

      /* Need to wait for the next frame */
      next_frame_no += 1;

      /* Figure out what the next frame time is */
      next_capture_ts = gst_util_uint64_scale (next_frame_no,
          fps_d * GST_SECOND, fps_n);

      id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (self),
          next_capture_ts + base_time);
      priv->clock_id = id;

      /* release the object lock while waiting */
      lk.unlock ();

      GST_LOG_OBJECT (self, "Waiting for next frame time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (next_capture_ts));
      clock_ret = gst_clock_id_wait (id, nullptr);
      lk.lock ();

      gst_clock_id_unref (id);
      priv->clock_id = nullptr;

      if (clock_ret == GST_CLOCK_UNSCHEDULED) {
        /* Got woken up by the unlock function */
        gst_clear_object (&clock);
        return GST_FLOW_FLUSHING;
      }
      /* Duration is a complete 1/fps frame duration */
      dur = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    } else {
      GstClockTime next_frame_ts;

      GST_LOG_OBJECT (self, "No need to wait for next frame time %"
          GST_TIME_FORMAT " next frame = %" G_GINT64_FORMAT " prev = %"
          G_GINT64_FORMAT, GST_TIME_ARGS (next_capture_ts), next_frame_no,
          priv->last_frame_no);

      next_frame_ts = gst_util_uint64_scale (next_frame_no + 1,
          fps_d * GST_SECOND, fps_n);
      /* Frame duration is from now until the next expected capture time */
      dur = next_frame_ts - next_capture_ts;
    }
  }
  gst_clear_object (&clock);

  priv->last_frame_no = next_frame_no;

  auto wgc = GST_D3D12_GRAPHICS_CAPTURE (priv->capture);
  GstBuffer *buffer = nullptr;
  guint width, height;
  CaptureCropRect crop_rect;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    crop_rect = priv->crop_rect;
  }

  auto ret = gst_d3d12_graphics_capture_do_capture (wgc,
      priv->downstream_supports_d3d12, &crop_rect, &buffer, &width, &height);
  if (ret != GST_FLOW_OK)
    return ret;

  if (width != priv->video_info.width || height != priv->video_info.height) {
    gst_video_info_set_format (&priv->video_info, GST_VIDEO_FORMAT_BGRA,
        width, height);
    priv->video_info.fps_n = fps_n;
    priv->video_info.fps_d = fps_d;

    auto caps = gst_pad_get_current_caps (bsrc->srcpad);
    if (!caps || gst_caps_is_any (caps)) {
      GST_ERROR_OBJECT (self, "Couldn't get current caps");
      gst_clear_caps (&caps);
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }

    caps = gst_caps_make_writable (caps);
    gst_caps_set_simple (caps, "width", G_TYPE_INT, width, "height", G_TYPE_INT,
        height, nullptr);

    auto caps_ret = gst_base_src_set_caps (bsrc, caps);
    gst_caps_unref (caps);
    if (!caps_ret) {
      GST_ERROR_OBJECT (self, "Couldn't update caps");
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }
  }

  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_PTS (buffer) = next_capture_ts;
  GST_BUFFER_DURATION (buffer) = dur;

  *buf = buffer;

  return GST_FLOW_OK;
}
#endif

static GstFlowReturn
gst_d3d12_screen_capture_src_dxgi_capture (GstBaseSrc * bsrc,
    guint64 offset, guint size, GstBuffer ** buf)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;
  gint fps_n, fps_d;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime base_time;
  GstClockTime next_capture_ts;
  GstClockTime latency;
  GstClockTime dur;
  guint64 next_frame_no;
  gboolean draw_mouse;
  /* Just magic number... */
  gint unsupported_retry_count = 100;
  GstBuffer *buffer = nullptr;
  D3D12_BOX crop_box;

  auto dxgi_capture = GST_D3D12_DXGI_CAPTURE (priv->capture);

  fps_n = GST_VIDEO_INFO_FPS_N (&priv->video_info);
  fps_d = GST_VIDEO_INFO_FPS_D (&priv->video_info);

  if (fps_n <= 0 || fps_d <= 0)
    return GST_FLOW_NOT_NEGOTIATED;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    draw_mouse = priv->show_cursor;
    gst_d3d12_screen_capture_src_get_crop_box (self, crop_box);
    if (crop_box.left != priv->crop_box.left ||
        crop_box.right != priv->crop_box.right ||
        crop_box.top != priv->crop_box.top ||
        crop_box.bottom != priv->crop_box.bottom) {
      GST_INFO_OBJECT (self, "Capture area changed, need negotiation");
      if (!gst_base_src_negotiate (bsrc)) {
        GST_ERROR_OBJECT (self, "Failed to negotiate with new capture area");
        return GST_FLOW_NOT_NEGOTIATED;
      }
    }
  }

again:
  auto clock = gst_element_get_clock (GST_ELEMENT_CAST (self));

  /* Check flushing before waiting clock because we are might be doing
   * retry */
  {
    std::unique_lock < std::mutex > lk (priv->flush_lock);
    if (priv->flushing) {
      gst_clear_object (&clock);
      return GST_FLOW_FLUSHING;
    }

    base_time = GST_ELEMENT_CAST (self)->base_time;
    next_capture_ts = gst_clock_get_time (clock);
    next_capture_ts -= base_time;

    next_frame_no = gst_util_uint64_scale (next_capture_ts,
        fps_n, GST_SECOND * fps_d);

    if (next_frame_no == priv->last_frame_no) {
      GstClockID id;
      GstClockReturn clock_ret;

      /* Need to wait for the next frame */
      next_frame_no += 1;

      /* Figure out what the next frame time is */
      next_capture_ts = gst_util_uint64_scale (next_frame_no,
          fps_d * GST_SECOND, fps_n);

      id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (self),
          next_capture_ts + base_time);
      priv->clock_id = id;

      /* release the object lock while waiting */
      lk.unlock ();

      GST_LOG_OBJECT (self, "Waiting for next frame time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (next_capture_ts));
      clock_ret = gst_clock_id_wait (id, nullptr);
      lk.lock ();

      gst_clock_id_unref (id);
      priv->clock_id = nullptr;

      if (clock_ret == GST_CLOCK_UNSCHEDULED) {
        /* Got woken up by the unlock function */
        gst_clear_object (&clock);
        return GST_FLOW_FLUSHING;
      }
      /* Duration is a complete 1/fps frame duration */
      dur = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    } else {
      GstClockTime next_frame_ts;

      GST_LOG_OBJECT (self, "No need to wait for next frame time %"
          GST_TIME_FORMAT " next frame = %" G_GINT64_FORMAT " prev = %"
          G_GINT64_FORMAT, GST_TIME_ARGS (next_capture_ts), next_frame_no,
          priv->last_frame_no);

      next_frame_ts = gst_util_uint64_scale (next_frame_no + 1,
          fps_d * GST_SECOND, fps_n);
      /* Frame duration is from now until the next expected capture time */
      dur = next_frame_ts - next_capture_ts;
    }
  }
  gst_clear_object (&clock);

  priv->last_frame_no = next_frame_no;

  if (!buffer) {
    if (priv->downstream_supports_d3d12) {
      ret = GST_BASE_SRC_CLASS (parent_class)->alloc (bsrc,
          offset, size, &buffer);
    } else {
      ret = gst_buffer_pool_acquire_buffer (priv->pool, &buffer, nullptr);
    }

    if (ret != GST_FLOW_OK)
      return ret;
  }

  auto before_capture = gst_util_get_timestamp ();
  ret = gst_d3d12_dxgi_capture_do_capture (dxgi_capture, buffer,
      &crop_box, draw_mouse);

  switch (ret) {
    case GST_D3D12_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR:
      GST_WARNING_OBJECT (self, "Got expected error, try again");
      goto again;
    case GST_D3D12_SCREEN_CAPTURE_FLOW_UNSUPPORTED:
      GST_WARNING_OBJECT (self, "Got DXGI_ERROR_UNSUPPORTED error");
      unsupported_retry_count--;

      if (unsupported_retry_count < 0) {
        gst_clear_buffer (&buffer);
        GST_ERROR_OBJECT (self, "too many DXGI_ERROR_UNSUPPORTED");
        return GST_FLOW_ERROR;
      }

      goto again;
    case GST_D3D12_SCREEN_CAPTURE_FLOW_SIZE_CHANGED:
      GST_INFO_OBJECT (self, "Size was changed, need negotiation");
      gst_clear_buffer (&buffer);

      if (!gst_base_src_negotiate (bsrc)) {
        GST_ERROR_OBJECT (self, "Failed to negotiate with new size");
        return GST_FLOW_NOT_NEGOTIATED;
      }
      goto again;
    default:
      break;
  }

  if (ret != GST_FLOW_OK) {
    gst_clear_buffer (&buffer);
    return ret;
  }

  if (!priv->downstream_supports_d3d12) {
    GstVideoFrame in_frame, out_frame;
    GstBuffer *sysmem_buf = nullptr;
    ret = GST_BASE_SRC_CLASS (parent_class)->alloc (bsrc,
        offset, size, &sysmem_buf);
    if (ret != GST_FLOW_OK) {
      gst_clear_buffer (&buffer);
      return ret;
    }

    if (!gst_video_frame_map (&in_frame, &priv->video_info, buffer,
            GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "Couldn't map frame");
      gst_clear_buffer (&buffer);
      gst_clear_buffer (&sysmem_buf);
      return GST_FLOW_ERROR;
    }

    if (!gst_video_frame_map (&out_frame, &priv->video_info, sysmem_buf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "Couldn't map frame");
      gst_video_frame_unmap (&in_frame);
      gst_clear_buffer (&buffer);
      gst_clear_buffer (&sysmem_buf);
      return GST_FLOW_ERROR;
    }

    auto copy_ret = gst_video_frame_copy (&out_frame, &in_frame);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&in_frame);

    if (!copy_ret) {
      GST_ERROR_OBJECT (self, "Couldn't copy frame");
      gst_clear_buffer (&buffer);
      gst_clear_buffer (&sysmem_buf);
      return GST_FLOW_ERROR;
    }

    gst_buffer_unref (buffer);
    buffer = sysmem_buf;
  }

  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_PTS (buffer) = next_capture_ts;
  GST_BUFFER_DURATION (buffer) = dur;

  auto after_capture = gst_util_get_timestamp ();
  latency = after_capture - before_capture;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!GST_CLOCK_TIME_IS_VALID (priv->latency) || priv->latency < latency) {
      priv->latency = latency;
      gst_element_post_message (GST_ELEMENT_CAST (self),
          gst_message_new_latency (GST_OBJECT_CAST (self)));
    }
  }

  *buf = buffer;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_screen_capture_src_create (GstBaseSrc * bsrc, guint64 offset,
    guint size, GstBuffer ** buf)
{
  auto self = GST_D3D12_SCREEN_CAPTURE_SRC (bsrc);
  auto priv = self->priv;

  if (!priv->capture) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Couldn't configure capture object"), (nullptr));
    return GST_FLOW_NOT_NEGOTIATED;
  }
#ifdef HAVE_WGC
  if (priv->selected_capture_api == GST_D3D12_SCREEN_CAPTURE_API_WGC)
    return gst_d3d12_screen_capture_src_wgc_capture (bsrc, offset, size, buf);
#endif

  return gst_d3d12_screen_capture_src_dxgi_capture (bsrc, offset, size, buf);
}
