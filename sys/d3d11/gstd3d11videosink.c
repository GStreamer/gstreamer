/* GStreamer
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

#include "gstd3d11videosink.h"
#include "gstd3d11memory.h"
#include "gstd3d11utils.h"
#include "gstd3d11device.h"
#include "gstd3d11bufferpool.h"
#include "gstd3d11format.h"
#include "gstd3d11videoprocessor.h"

#if GST_D3D11_WINAPI_ONLY_APP
#include "gstd3d11window_corewindow.h"
#include "gstd3d11window_swapchainpanel.h"
#else
#include "gstd3d11window_win32.h"
#endif

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS,
  PROP_FULLSCREEN_TOGGLE_MODE,
  PROP_FULLSCREEN,
};

#define DEFAULT_ADAPTER                   -1
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE
#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE
#define DEFAULT_FULLSCREEN_TOGGLE_MODE    GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE
#define DEFAULT_FULLSCREEN                FALSE

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D11_FORMATS)
    ));

GST_DEBUG_CATEGORY (d3d11_video_sink_debug);
#define GST_CAT_DEFAULT d3d11_video_sink_debug

static void gst_d3d11_videosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_videosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_d3d11_video_sink_video_overlay_init (GstVideoOverlayInterface * iface);
static void
gst_d3d11_video_sink_navigation_init (GstNavigationInterface * iface);

static void gst_d3d11_video_sink_set_context (GstElement * element,
    GstContext * context);
static GstCaps *gst_d3d11_video_sink_get_caps (GstBaseSink * sink,
    GstCaps * filter);
static gboolean gst_d3d11_video_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);

static gboolean gst_d3d11_video_sink_start (GstBaseSink * sink);
static gboolean gst_d3d11_video_sink_stop (GstBaseSink * sink);
static gboolean gst_d3d11_video_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query);
static gboolean gst_d3d11_video_sink_query (GstBaseSink * sink,
    GstQuery * query);
static gboolean gst_d3d11_video_sink_unlock (GstBaseSink * sink);
static gboolean gst_d3d11_video_sink_unlock_stop (GstBaseSink * sink);

static GstFlowReturn
gst_d3d11_video_sink_show_frame (GstVideoSink * sink, GstBuffer * buf);

#define gst_d3d11_video_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstD3D11VideoSink, gst_d3d11_video_sink,
    GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_d3d11_video_sink_video_overlay_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
        gst_d3d11_video_sink_navigation_init);
    GST_DEBUG_CATEGORY_INIT (d3d11_video_sink_debug,
        "d3d11videosink", 0, "Direct3D11 Video Sink"));

static void
gst_d3d11_video_sink_class_init (GstD3D11VideoSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *videosink_class = GST_VIDEO_SINK_CLASS (klass);

  gobject_class->set_property = gst_d3d11_videosink_set_property;
  gobject_class->get_property = gst_d3d11_videosink_get_property;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLE_NAVIGATION_EVENTS,
      g_param_spec_boolean ("enable-navigation-events",
          "Enable navigation events",
          "When enabled, navigation events are sent upstream",
          DEFAULT_ENABLE_NAVIGATION_EVENTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN_TOGGLE_MODE,
      g_param_spec_flags ("fullscreen-toggle-mode",
          "Full screen toggle mode",
          "Full screen toggle mode used to trigger fullscreen mode change",
          GST_D3D11_WINDOW_TOGGLE_MODE_GET_TYPE, DEFAULT_FULLSCREEN_TOGGLE_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen",
          "fullscreen",
          "Ignored when \"fullscreen-toggle-mode\" does not include \"property\"",
          DEFAULT_FULLSCREEN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 video sink", "Sink/Video",
      "A Direct3D11 based videosink",
      "Seungha Yang <seungha.yang@navercorp.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  basesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_get_caps);
  basesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_set_caps);
  basesink_class->start = GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_start);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_stop);
  basesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_propose_allocation);
  basesink_class->query = GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_query);
  basesink_class->unlock = GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_unlock);
  basesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_unlock_stop);

  videosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_show_frame);

  gst_type_mark_as_plugin_api (GST_D3D11_WINDOW_TOGGLE_MODE_GET_TYPE, 0);
}

static void
gst_d3d11_video_sink_init (GstD3D11VideoSink * self)
{
  self->adapter = DEFAULT_ADAPTER;
  self->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  self->enable_navigation_events = DEFAULT_ENABLE_NAVIGATION_EVENTS;
  self->fullscreen_toggle_mode = DEFAULT_FULLSCREEN_TOGGLE_MODE;
  self->fullscreen = DEFAULT_FULLSCREEN;
}

static void
gst_d3d11_videosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_ADAPTER:
      self->adapter = g_value_get_int (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      self->force_aspect_ratio = g_value_get_boolean (value);
      if (self->window)
        g_object_set (self->window,
            "force-aspect-ratio", self->force_aspect_ratio, NULL);
      break;
    case PROP_ENABLE_NAVIGATION_EVENTS:
      self->enable_navigation_events = g_value_get_boolean (value);
      if (self->window) {
        g_object_set (self->window,
            "enable-navigation-events", self->enable_navigation_events, NULL);
      }
      break;
    case PROP_FULLSCREEN_TOGGLE_MODE:
      self->fullscreen_toggle_mode = g_value_get_flags (value);
      if (self->window) {
        g_object_set (self->window,
            "fullscreen-toggle-mode", self->fullscreen_toggle_mode, NULL);
      }
      break;
    case PROP_FULLSCREEN:
      self->fullscreen = g_value_get_boolean (value);
      if (self->window) {
        g_object_set (self->window, "fullscreen", self->fullscreen, NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_d3d11_videosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, self->adapter);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, self->force_aspect_ratio);
      break;
    case PROP_ENABLE_NAVIGATION_EVENTS:
      g_value_set_boolean (value, self->enable_navigation_events);
      break;
    case PROP_FULLSCREEN_TOGGLE_MODE:
      g_value_set_flags (value, self->fullscreen_toggle_mode);
      break;
    case PROP_FULLSCREEN:
      if (self->window) {
        g_object_get_property (G_OBJECT (self->window), pspec->name, value);
      } else {
        g_value_set_boolean (value, self->fullscreen);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_video_sink_set_context (GstElement * element, GstContext * context)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (element);

  gst_d3d11_handle_set_context (element, context, self->adapter, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static GstCaps *
gst_d3d11_video_sink_get_caps (GstBaseSink * sink, GstCaps * filter)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  GstCaps *caps = NULL;

  if (self->device && !self->can_convert) {
    GstCaps *overlaycaps;
    GstCapsFeatures *features;

    caps = gst_d3d11_device_get_supported_caps (self->device,
        D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DISPLAY);
    overlaycaps = gst_caps_copy (caps);
    features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, NULL);
    gst_caps_set_features_simple (overlaycaps, features);
    gst_caps_append (caps, overlaycaps);
  }

  if (!caps)
    caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (sink));

  if (caps && filter) {
    GstCaps *isect;
    isect = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = isect;
  }

  return caps;
}

static gboolean
gst_d3d11_video_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  gint video_width, video_height;
  gint video_par_n, video_par_d;        /* video's PAR */
  gint display_par_n = 1, display_par_d = 1;    /* display's PAR */
  guint num, den;
  GError *error = NULL;
  GstStructure *config;
  gint i;

  GST_DEBUG_OBJECT (self, "set caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&self->info, caps))
    goto invalid_format;

  video_width = GST_VIDEO_INFO_WIDTH (&self->info);
  video_height = GST_VIDEO_INFO_HEIGHT (&self->info);
  video_par_n = GST_VIDEO_INFO_PAR_N (&self->info);
  video_par_d = GST_VIDEO_INFO_PAR_D (&self->info);

  /* get aspect ratio from caps if it's present, and
   * convert video width and height to a display width and height
   * using wd / hd = wv / hv * PARv / PARd */

  /* TODO: Get display PAR */

  if (!gst_video_calculate_display_ratio (&num, &den, video_width,
          video_height, video_par_n, video_par_d, display_par_n, display_par_d))
    goto no_disp_ratio;

  GST_DEBUG_OBJECT (sink,
      "video width/height: %dx%d, calculated display ratio: %d/%d format: %s",
      video_width, video_height, num, den,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&self->info)));

  /* now find a width x height that respects this display ratio.
   * prefer those that have one of w/h the same as the incoming video
   * using wd / hd = num / den
   */

  /* start with same height, because of interlaced video
   * check hd / den is an integer scale factor, and scale wd with the PAR
   */
  if (video_height % den == 0) {
    GST_DEBUG_OBJECT (self, "keeping video height");
    GST_VIDEO_SINK_WIDTH (self) = (guint)
        gst_util_uint64_scale_int (video_height, num, den);
    GST_VIDEO_SINK_HEIGHT (self) = video_height;
  } else if (video_width % num == 0) {
    GST_DEBUG_OBJECT (self, "keeping video width");
    GST_VIDEO_SINK_WIDTH (self) = video_width;
    GST_VIDEO_SINK_HEIGHT (self) = (guint)
        gst_util_uint64_scale_int (video_width, den, num);
  } else {
    GST_DEBUG_OBJECT (self, "approximating while keeping video height");
    GST_VIDEO_SINK_WIDTH (self) = (guint)
        gst_util_uint64_scale_int (video_height, num, den);
    GST_VIDEO_SINK_HEIGHT (self) = video_height;
  }

  GST_DEBUG_OBJECT (self, "scaling to %dx%d",
      GST_VIDEO_SINK_WIDTH (self), GST_VIDEO_SINK_HEIGHT (self));
  self->video_width = video_width;
  self->video_height = video_height;

  if (GST_VIDEO_SINK_WIDTH (self) <= 0 || GST_VIDEO_SINK_HEIGHT (self) <= 0)
    goto no_display_size;

  GST_OBJECT_LOCK (self);
  if (!self->pending_render_rect) {
    self->render_rect.x = 0;
    self->render_rect.y = 0;
    self->render_rect.w = GST_VIDEO_SINK_WIDTH (self);
    self->render_rect.h = GST_VIDEO_SINK_HEIGHT (self);
  }

  gst_d3d11_window_set_render_rectangle (self->window,
      self->render_rect.x, self->render_rect.y, self->render_rect.w,
      self->render_rect.h);
  self->pending_render_rect = FALSE;
  GST_OBJECT_UNLOCK (self);

  self->have_video_processor = FALSE;
  if (!gst_d3d11_window_prepare (self->window, GST_VIDEO_SINK_WIDTH (self),
          GST_VIDEO_SINK_HEIGHT (self), caps, &self->have_video_processor,
          &error)) {
    GstMessage *error_msg;

    GST_ERROR_OBJECT (self, "cannot create swapchain");
    error_msg = gst_message_new_error (GST_OBJECT_CAST (self),
        error, "Failed to prepare d3d11window");
    g_clear_error (&error);
    gst_element_post_message (GST_ELEMENT (self), error_msg);

    return FALSE;
  }

  if (self->fallback_pool) {
    gst_buffer_pool_set_active (self->fallback_pool, FALSE);
    gst_object_unref (self->fallback_pool);
  }

  self->fallback_pool = gst_d3d11_buffer_pool_new (self->device);
  config = gst_buffer_pool_get_config (self->fallback_pool);
  gst_buffer_pool_config_set_params (config,
      caps, GST_VIDEO_INFO_SIZE (&self->info), 0, 2);

  {
    GstD3D11AllocationParams *d3d11_params;
    gint bind_flags = D3D11_BIND_SHADER_RESOURCE;

    if (self->have_video_processor) {
      /* To create video processor input view, one of following bind flags
       * is required
       * NOTE: Any texture arrays which were created with D3D11_BIND_DECODER flag
       * cannot be used for shader input.
       *
       * D3D11_BIND_DECODER
       * D3D11_BIND_VIDEO_ENCODER
       * D3D11_BIND_RENDER_TARGET
       * D3D11_BIND_UNORDERED_ACCESS_VIEW
       */
      bind_flags |= D3D11_BIND_RENDER_TARGET;
    }

    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params) {
      d3d11_params = gst_d3d11_allocation_params_new (self->device,
          &self->info, 0, bind_flags);
    } else {
      /* Set bind flag */
      for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&self->info); i++) {
        d3d11_params->desc[i].BindFlags |= bind_flags;
      }
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);
  }

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (self->fallback_pool, config);

  return TRUE;

  /* ERRORS */
invalid_format:
  {
    GST_DEBUG_OBJECT (sink,
        "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
no_disp_ratio:
  {
    GST_ELEMENT_ERROR (sink, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }
no_display_size:
  {
    GST_ELEMENT_ERROR (sink, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }
}

static void
gst_d3d11_video_sink_key_event (GstD3D11Window * window, const gchar * event,
    const gchar * key, GstD3D11VideoSink * self)
{
  if (self->enable_navigation_events) {
    GST_LOG_OBJECT (self, "send key event %s, key %s", event, key);
    gst_navigation_send_key_event (GST_NAVIGATION (self), event, key);
  }
}

static void
gst_d3d11_video_mouse_key_event (GstD3D11Window * window, const gchar * event,
    gint button, gdouble x, gdouble y, GstD3D11VideoSink * self)
{
  if (self->enable_navigation_events) {
    GST_LOG_OBJECT (self,
        "send mouse event %s, button %d (%.1f, %.1f)", event, button, x, y);
    gst_navigation_send_mouse_event (GST_NAVIGATION (self), event, button, x,
        y);
  }
}

static gboolean
gst_d3d11_video_sink_start (GstBaseSink * sink)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  gboolean is_hardware = TRUE;
  GstD3D11WindowNativeType window_type = GST_D3D11_WINDOW_NATIVE_TYPE_HWND;

  GST_DEBUG_OBJECT (self, "Start");

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), self->adapter,
          &self->device)) {
    GST_ERROR_OBJECT (sink, "Cannot create d3d11device");
    return FALSE;
  }

  if (!self->window_id)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (self));

  if (self->window_id) {
    window_type =
        gst_d3d11_window_get_native_type_from_handle (self->window_id);

    if (window_type != GST_D3D11_WINDOW_NATIVE_TYPE_NONE) {
      gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (self),
          self->window_id);
    }
  }

  GST_DEBUG_OBJECT (self, "Create window (type: %s)",
      gst_d3d11_window_get_native_type_to_string (window_type));

#if GST_D3D11_WINAPI_ONLY_APP
  if (window_type != GST_D3D11_WINDOW_NATIVE_TYPE_CORE_WINDOW &&
      window_type != GST_D3D11_WINDOW_NATIVE_TYPE_SWAP_CHAIN_PANEL) {
    GST_ERROR_OBJECT (sink, "Overlay handle must be set before READY state");
    return FALSE;
  }
#endif

  switch (window_type) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    case GST_D3D11_WINDOW_NATIVE_TYPE_HWND:
      self->window = gst_d3d11_window_win32_new (self->device, self->window_id);
      break;
#else
    case GST_D3D11_WINDOW_NATIVE_TYPE_CORE_WINDOW:
      self->window = gst_d3d11_window_core_window_new (self->device,
          self->window_id);
      break;
    case GST_D3D11_WINDOW_NATIVE_TYPE_SWAP_CHAIN_PANEL:
      self->window = gst_d3d11_window_swap_chain_panel_new (self->device,
          self->window_id);
      break;
#endif
    default:
      break;
  }

  if (!self->window) {
    GST_ERROR_OBJECT (sink, "Cannot create d3d11window");
    return FALSE;
  }

  GST_OBJECT_LOCK (self);
  g_object_set (self->window,
      "force-aspect-ratio", self->force_aspect_ratio,
      "fullscreen-toggle-mode", self->fullscreen_toggle_mode,
      "fullscreen", self->fullscreen,
      "enable-navigation-events", self->enable_navigation_events, NULL);
  GST_OBJECT_UNLOCK (self);

  g_signal_connect (self->window, "key-event",
      G_CALLBACK (gst_d3d11_video_sink_key_event), self);
  g_signal_connect (self->window, "mouse-event",
      G_CALLBACK (gst_d3d11_video_mouse_key_event), self);

  g_object_get (self->device, "hardware", &is_hardware, NULL);

  if (!is_hardware) {
    GST_WARNING_OBJECT (self, "D3D11 device is running on software emulation");
    self->can_convert = FALSE;
  } else {
    self->can_convert = TRUE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_stop (GstBaseSink * sink)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);

  GST_DEBUG_OBJECT (self, "Stop");

  if (self->fallback_pool) {
    gst_buffer_pool_set_active (self->fallback_pool, FALSE);
    gst_object_unref (self->fallback_pool);
    self->fallback_pool = NULL;
  }

  if (self->window)
    gst_d3d11_window_unprepare (self->window);

  gst_clear_object (&self->device);
  gst_clear_object (&self->window);

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  GstStructure *config;
  GstCaps *caps;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  guint size;
  gboolean need_pool;

  if (!self->device || !self->window)
    return FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  /* the normal size of a frame */
  size = info.size;

  if (need_pool) {
    gint i;
    GstD3D11AllocationParams *d3d11_params;

    GST_DEBUG_OBJECT (self, "create new pool");

    pool = gst_d3d11_buffer_pool_new (self->device);
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 2,
        DXGI_MAX_SWAP_CHAIN_BUFFERS);

    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params) {
      d3d11_params = gst_d3d11_allocation_params_new (self->device, &info, 0,
          D3D11_BIND_SHADER_RESOURCE);
    } else {
      /* Set bind flag */
      for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
        d3d11_params->desc[i].BindFlags |= D3D11_BIND_SHADER_RESOURCE;
      }
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (!gst_buffer_pool_set_config (pool, config)) {
      g_object_unref (pool);
      goto config_failed;
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 2, 0);
  if (pool)
    g_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_WARNING_OBJECT (self, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_WARNING_OBJECT (self, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_WARNING_OBJECT (self, "failed setting config");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_query (GstBaseSink * sink, GstQuery * query)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (self), query,
              self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->query (sink, query);
}

static gboolean
gst_d3d11_video_sink_unlock (GstBaseSink * sink)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);

  if (self->window)
    gst_d3d11_window_unlock (self->window);

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_unlock_stop (GstBaseSink * sink)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);

  if (self->window)
    gst_d3d11_window_unlock_stop (self->window);

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_upload_frame (GstD3D11VideoSink * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;
  gint i;

  GST_LOG_OBJECT (self, "Copy to fallback buffer");

  if (!gst_video_frame_map (&in_frame, &self->info, inbuf,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, &self->info, outbuf,
          GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  if (ret) {
    /* map to upload staging texture to render texture */
    for (i = 0; i < gst_buffer_n_memory (outbuf); i++) {
      GstMemory *mem;
      GstMapInfo map;

      mem = gst_buffer_peek_memory (outbuf, i);
      if (!gst_memory_map (mem, &map, (GST_MAP_READ | GST_MAP_D3D11))) {
        GST_ERROR_OBJECT (self, "cannot upload staging texture");
        ret = FALSE;
        break;
      }
      gst_memory_unmap (mem, &map);
    }
  }

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    GST_ELEMENT_WARNING (self, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    return FALSE;
  }
}

static gboolean
gst_d3d11_video_sink_copy_d3d11_to_d3d11 (GstD3D11VideoSink * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  gint i;
  ID3D11DeviceContext *context_handle =
      gst_d3d11_device_get_device_context_handle (self->device);

  g_return_val_if_fail (gst_buffer_n_memory (inbuf) ==
      gst_buffer_n_memory (outbuf), FALSE);

  GST_LOG_OBJECT (self, "Copy to fallback buffer using device memory copy");

  gst_d3d11_device_lock (self->device);
  for (i = 0; i < gst_buffer_n_memory (inbuf); i++) {
    GstD3D11Memory *in_mem =
        (GstD3D11Memory *) gst_buffer_peek_memory (inbuf, i);
    GstD3D11Memory *out_mem =
        (GstD3D11Memory *) gst_buffer_peek_memory (outbuf, i);
    D3D11_BOX src_box;

    /* input buffer might be larger than render size */
    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = out_mem->desc.Width;
    src_box.bottom = out_mem->desc.Height;

    ID3D11DeviceContext_CopySubresourceRegion (context_handle,
        (ID3D11Resource *) out_mem->texture, 0, 0, 0, 0,
        (ID3D11Resource *) in_mem->texture, in_mem->subresource_index,
        &src_box);
  }
  gst_d3d11_device_unlock (self->device);

  return TRUE;
}

static GstFlowReturn
gst_d3d11_video_sink_show_frame (GstVideoSink * sink, GstBuffer * buf)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  GstMapInfo map;
  GstFlowReturn ret;
  GstVideoRectangle rect = { 0, };
  GstBuffer *render_buf;
  gboolean need_unref = FALSE;
  gboolean do_device_copy = TRUE;
  gint i;

  render_buf = buf;

  for (i = 0; i < gst_buffer_n_memory (buf); i++) {
    GstMemory *mem;
    GstD3D11Memory *dmem;

    mem = gst_buffer_peek_memory (buf, i);
    if (!gst_is_d3d11_memory (mem)) {
      GST_LOG_OBJECT (sink, "not a d3d11 memory, need fallback");
      render_buf = NULL;
      do_device_copy = FALSE;
      break;
    }

    dmem = (GstD3D11Memory *) mem;
    if (dmem->device != self->device) {
      GST_LOG_OBJECT (sink, "different d3d11 device, need fallback");
      render_buf = NULL;
      do_device_copy = FALSE;
      break;
    }

    if (dmem->desc.Usage == D3D11_USAGE_DEFAULT) {
      if (!gst_memory_map (mem, &map, (GST_MAP_READ | GST_MAP_D3D11))) {
        GST_ERROR_OBJECT (self, "cannot map d3d11 memory");
        return GST_FLOW_ERROR;
      }

      gst_memory_unmap (mem, &map);
    }

    if (gst_buffer_n_memory (buf) == 1 && self->have_video_processor &&
        gst_d3d11_video_processor_check_bind_flags_for_input_view
        (dmem->desc.BindFlags)) {
      break;
    }

    if (!gst_d3d11_memory_ensure_shader_resource_view (dmem)) {
      GST_LOG_OBJECT (sink,
          "shader resource view is unavailable, need fallback");
      render_buf = NULL;
      /* keep run loop in order to upload staging memory to device memory */
    }
  }

  if (!render_buf) {
    if (!self->fallback_pool ||
        !gst_buffer_pool_set_active (self->fallback_pool, TRUE) ||
        gst_buffer_pool_acquire_buffer (self->fallback_pool, &render_buf,
            NULL) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "fallback pool is unavailable");

      return GST_FLOW_ERROR;
    }

    for (i = 0; i < gst_buffer_n_memory (render_buf); i++) {
      GstD3D11Memory *dmem;

      dmem = (GstD3D11Memory *) gst_buffer_peek_memory (render_buf, i);
      if (!gst_d3d11_memory_ensure_shader_resource_view (dmem)) {
        GST_ERROR_OBJECT (self, "fallback shader resource view is unavailable");
        gst_buffer_unref (render_buf);

        return GST_FLOW_ERROR;
      }
    }

    if (do_device_copy) {
      if (!gst_d3d11_video_sink_copy_d3d11_to_d3d11 (self, buf, render_buf)) {
        GST_ERROR_OBJECT (self, "cannot copy frame");
        gst_buffer_unref (render_buf);

        return GST_FLOW_ERROR;
      }
    } else if (!gst_d3d11_video_sink_upload_frame (self, buf, render_buf)) {
      GST_ERROR_OBJECT (self, "cannot upload frame");
      gst_buffer_unref (render_buf);

      return GST_FLOW_ERROR;
    }

    need_unref = TRUE;
  }

  gst_d3d11_window_show (self->window);

  /* FIXME: add support crop meta */
  rect.w = self->video_width;
  rect.h = self->video_height;

  ret = gst_d3d11_window_render (self->window, render_buf, &rect);
  if (need_unref)
    gst_buffer_unref (render_buf);

  if (ret == GST_D3D11_WINDOW_FLOW_CLOSED) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Output window was closed"), (NULL));

    ret = GST_FLOW_ERROR;
  }

  return ret;
}

/* VideoOverlay interface */
static void
gst_d3d11_video_sink_set_window_handle (GstVideoOverlay * overlay,
    guintptr window_id)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (overlay);

  GST_DEBUG ("set window handle %" G_GUINTPTR_FORMAT, window_id);

  self->window_id = window_id;
}

static void
gst_d3d11_video_sink_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (overlay);

  GST_DEBUG_OBJECT (self,
      "render rect x: %d, y: %d, width: %d, height %d", x, y, width, height);

  GST_OBJECT_LOCK (self);
  if (self->window) {
    gst_d3d11_window_set_render_rectangle (self->window, x, y, width, height);
  } else {
    self->render_rect.x = x;
    self->render_rect.y = y;
    self->render_rect.w = width;
    self->render_rect.h = height;
    self->pending_render_rect = TRUE;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_d3d11_video_sink_expose (GstVideoOverlay * overlay)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (overlay);

  if (self->window && self->window->swap_chain) {
    GstVideoRectangle rect = { 0, };
    rect.w = GST_VIDEO_SINK_WIDTH (self);
    rect.h = GST_VIDEO_SINK_HEIGHT (self);

    gst_d3d11_window_render (self->window, NULL, &rect);
  }
}

static void
gst_d3d11_video_sink_video_overlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_d3d11_video_sink_set_window_handle;
  iface->set_render_rectangle = gst_d3d11_video_sink_set_render_rectangle;
  iface->expose = gst_d3d11_video_sink_expose;
}

/* Navigation interface */
static void
gst_d3d11_video_sink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (navigation);
  gboolean handled = FALSE;
  GstEvent *event = NULL;
  GstVideoRectangle src = { 0, };
  GstVideoRectangle dst = { 0, };
  GstVideoRectangle result;
  gdouble x, y, xscale = 1.0, yscale = 1.0;

  if (!self->window) {
    gst_structure_free (structure);
    return;
  }

  if (self->force_aspect_ratio) {
    /* We get the frame position using the calculated geometry from _setcaps
       that respect pixel aspect ratios */
    src.w = GST_VIDEO_SINK_WIDTH (self);
    src.h = GST_VIDEO_SINK_HEIGHT (self);
    dst.w = self->render_rect.w;
    dst.h = self->render_rect.h;

    gst_video_sink_center_rect (src, dst, &result, TRUE);
    result.x += self->render_rect.x;
    result.y += self->render_rect.y;
  } else {
    memcpy (&result, &self->render_rect, sizeof (GstVideoRectangle));
  }

  xscale = (gdouble) GST_VIDEO_INFO_WIDTH (&self->info) / result.w;
  yscale = (gdouble) GST_VIDEO_INFO_HEIGHT (&self->info) / result.h;

  /* Converting pointer coordinates to the non scaled geometry */
  if (gst_structure_get_double (structure, "pointer_x", &x)) {
    x = MIN (x, result.x + result.w);
    x = MAX (x - result.x, 0);
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
        (gdouble) x * xscale, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &y)) {
    y = MIN (y, result.y + result.h);
    y = MAX (y - result.y, 0);
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
        (gdouble) y * yscale, NULL);
  }

  event = gst_event_new_navigation (structure);
  if (event) {
    gst_event_ref (event);
    handled = gst_pad_push_event (GST_VIDEO_SINK_PAD (self), event);

    if (!handled)
      gst_element_post_message (GST_ELEMENT_CAST (self),
          gst_navigation_message_new_event (GST_OBJECT_CAST (self), event));

    gst_event_unref (event);
  }
}

static void
gst_d3d11_video_sink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_d3d11_video_sink_navigation_send_event;
}
