/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-d3d11videosink
 * @title: d3d11videosink
 *
 * Direct3D11 based video render element
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! d3d11upload ! d3d11videosink
 * ```
 * This pipeline will display test video stream on screen via d3d11videosink
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d11videosink.h"
#include "gstd3d11videoprocessor.h"
#include "gstd3d11pluginutils.h"
#include <string>

#if GST_D3D11_WINAPI_APP
#include "gstd3d11window_corewindow.h"
#include "gstd3d11window_swapchainpanel.h"
#endif
#if (!GST_D3D11_WINAPI_ONLY_APP)
#include "gstd3d11window_win32.h"
#endif
#include "gstd3d11window_dummy.h"

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS,
  PROP_FULLSCREEN_TOGGLE_MODE,
  PROP_FULLSCREEN,
  PROP_DRAW_ON_SHARED_TEXTURE,
};

#define DEFAULT_ADAPTER                   -1
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE
#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE
#define DEFAULT_FULLSCREEN_TOGGLE_MODE    GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE
#define DEFAULT_FULLSCREEN                FALSE
#define DEFAULT_DRAW_ON_SHARED_TEXTURE    FALSE

enum
{
  /* signals */
  SIGNAL_BEGIN_DRAW,

  /* actions */
  SIGNAL_DRAW,

  LAST_SIGNAL
};

static guint gst_d3d11_video_sink_signals[LAST_SIGNAL] = { 0, };

static GstStaticCaps pad_template_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_SINK_FORMATS) "; "
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_D3D11_SINK_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE (GST_D3D11_SINK_FORMATS) "; "
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_D3D11_SINK_FORMATS));

GST_DEBUG_CATEGORY (d3d11_video_sink_debug);
#define GST_CAT_DEFAULT d3d11_video_sink_debug

struct _GstD3D11VideoSink
{
  GstVideoSink parent;
  GstD3D11Device *device;
  GstD3D11Window *window;
  gint video_width;
  gint video_height;

  GstVideoInfo info;

  guintptr window_id;

  gboolean caps_updated;

  /* properties */
  gint adapter;
  gboolean force_aspect_ratio;
  gboolean enable_navigation_events;
  GstD3D11WindowFullscreenToggleMode fullscreen_toggle_mode;
  gboolean fullscreen;
  gboolean draw_on_shared_texture;

  /* saved render rectangle until we have a window */
  GstVideoRectangle render_rect;
  gboolean pending_render_rect;

  GstBufferPool *fallback_pool;
  gboolean have_video_processor;
  gboolean processor_in_use;

  /* For drawing on user texture */
  gboolean drawing;
  GstBuffer *current_buffer;
  GRecMutex draw_lock;

  gchar *title;
};

static void gst_d3d11_videosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_videosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_video_sink_finalize (GObject * object);
static gboolean
gst_d3d11_video_sink_draw_action (GstD3D11VideoSink * self,
    gpointer shared_handle, guint texture_misc_flags, guint64 acquire_key,
    guint64 release_key);

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
static gboolean gst_d3d11_video_sink_event (GstBaseSink * sink,
    GstEvent * event);

static GstFlowReturn
gst_d3d11_video_sink_show_frame (GstVideoSink * sink, GstBuffer * buf);
static gboolean gst_d3d11_video_sink_prepare_window (GstD3D11VideoSink * self);

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
  GstCaps *caps;

  gobject_class->set_property = gst_d3d11_videosink_set_property;
  gobject_class->get_property = gst_d3d11_videosink_get_property;
  gobject_class->finalize = gst_d3d11_video_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
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
          "When enabled, navigation events are sent upstream",
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

  /**
   * GstD3D11VideoSink:draw-on-shared-texture:
   *
   * Instruct the sink to draw on a shared texture provided by user.
   * User must watch #d3d11videosink::begin-draw signal and should call
   * #d3d11videosink::draw method on the #d3d11videosink::begin-draw
   * signal handler.
   *
   * Currently supported formats for user texture are:
   * - DXGI_FORMAT_R8G8B8A8_UNORM
   * - DXGI_FORMAT_B8G8R8A8_UNORM
   * - DXGI_FORMAT_R10G10B10A2_UNORM
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_DRAW_ON_SHARED_TEXTURE,
      g_param_spec_boolean ("draw-on-shared-texture",
          "Draw on shared texture",
          "Draw on user provided shared texture instead of window. "
          "When enabled, user can pass application's own texture to sink "
          "by using \"draw\" action signal on \"begin-draw\" signal handler, "
          "so that sink can draw video data on application's texture. "
          "Supported texture formats for user texture are "
          "DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, and "
          "DXGI_FORMAT_R10G10B10A2_UNORM.",
          DEFAULT_DRAW_ON_SHARED_TEXTURE,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  /**
   * GstD3D11VideoSink::begin-draw:
   * @videosink: the #d3d11videosink
   *
   * Emitted when sink has a texture to draw. Application needs to invoke
   * #d3d11videosink::draw action signal before returning from
   * #d3d11videosink::begin-draw signal handler.
   *
   * Since: 1.20
   */
  gst_d3d11_video_sink_signals[SIGNAL_BEGIN_DRAW] =
      g_signal_new ("begin-draw", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstD3D11VideoSinkClass, begin_draw),
      NULL, NULL, NULL, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstD3D11VideoSink::draw:
   * @videosink: the #d3d11videosink
   * @shard_handle: a pointer to HANDLE
   * @texture_misc_flags: a D3D11_RESOURCE_MISC_FLAG value
   * @acquire_key: a key value used for IDXGIKeyedMutex::AcquireSync
   * @release_key: a key value used for IDXGIKeyedMutex::ReleaseSync
   *
   * Draws on a shared texture. @shard_handle must be a valid pointer to
   * a HANDLE which was obtained via IDXGIResource::GetSharedHandle or
   * IDXGIResource1::CreateSharedHandle.
   *
   * If the texture was created with D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX flag,
   * caller must specify valid @acquire_key and @release_key.
   * Otherwise (i.e., created with D3D11_RESOURCE_MISC_SHARED flag),
   * @acquire_key and @release_key will be ignored.
   *
   * Since: 1.20
   */
  gst_d3d11_video_sink_signals[SIGNAL_DRAW] =
      g_signal_new ("draw", G_TYPE_FROM_CLASS (klass),
      (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_STRUCT_OFFSET (GstD3D11VideoSinkClass, draw), NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 4, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT64,
      G_TYPE_UINT64);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 video sink", "Sink/Video",
      "A Direct3D11 based videosink",
      "Seungha Yang <seungha.yang@navercorp.com>");

  caps = gst_d3d11_get_updated_template_caps (&pad_template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
  gst_caps_unref (caps);

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
  basesink_class->event = GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_event);

  videosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_show_frame);

  klass->draw = gst_d3d11_video_sink_draw_action;

  gst_type_mark_as_plugin_api (GST_D3D11_WINDOW_TOGGLE_MODE_GET_TYPE,
      (GstPluginAPIFlags) 0);
}

static void
gst_d3d11_video_sink_init (GstD3D11VideoSink * self)
{
  self->adapter = DEFAULT_ADAPTER;
  self->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  self->enable_navigation_events = DEFAULT_ENABLE_NAVIGATION_EVENTS;
  self->fullscreen_toggle_mode = DEFAULT_FULLSCREEN_TOGGLE_MODE;
  self->fullscreen = DEFAULT_FULLSCREEN;
  self->draw_on_shared_texture = DEFAULT_DRAW_ON_SHARED_TEXTURE;

  g_rec_mutex_init (&self->draw_lock);
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
      self->fullscreen_toggle_mode =
          (GstD3D11WindowFullscreenToggleMode) g_value_get_flags (value);
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
    case PROP_DRAW_ON_SHARED_TEXTURE:
      self->draw_on_shared_texture = g_value_get_boolean (value);
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
    case PROP_DRAW_ON_SHARED_TEXTURE:
      g_value_set_boolean (value, self->draw_on_shared_texture);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_video_sink_finalize (GObject * object)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (object);

  g_rec_mutex_clear (&self->draw_lock);
  g_free (self->title);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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

  caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (sink));

  if (self->device) {
    gboolean is_hardware = FALSE;

    g_object_get (self->device, "hardware", &is_hardware, NULL);

    /* In case of WARP device, conversion via shader would be inefficient than
     * upstream videoconvert. Allow native formats in this case */
    if (!is_hardware) {
      GValue format_list = G_VALUE_INIT;
      GValue format = G_VALUE_INIT;

      g_value_init (&format_list, GST_TYPE_LIST);
      g_value_init (&format, G_TYPE_STRING);

      g_value_set_string (&format, "RGBA");
      gst_value_list_append_and_take_value (&format_list, &format);

      format = G_VALUE_INIT;
      g_value_init (&format, G_TYPE_STRING);
      g_value_set_string (&format, "BGRA");
      gst_value_list_append_and_take_value (&format_list, &format);

      caps = gst_caps_make_writable (caps);
      gst_caps_set_value (caps, "format", &format_list);
      g_value_unset (&format_list);
    }
  }

  if (filter) {
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

  GST_DEBUG_OBJECT (self, "set caps %" GST_PTR_FORMAT, caps);

  /* We will update window on show_frame() */
  self->caps_updated = TRUE;

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_update_window (GstD3D11VideoSink * self, GstCaps * caps)
{
  gint video_width, video_height;
  gint video_par_n, video_par_d;        /* video's PAR */
  gint display_par_n = 1, display_par_d = 1;    /* display's PAR */
  guint num, den;
  GError *error = NULL;

  GST_DEBUG_OBJECT (self, "Updating window with caps %" GST_PTR_FORMAT, caps);

  self->caps_updated = FALSE;

  if (!gst_d3d11_video_sink_prepare_window (self))
    goto no_window;

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

  GST_DEBUG_OBJECT (self,
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
  if (self->pending_render_rect) {
    GstVideoRectangle rect = self->render_rect;

    self->pending_render_rect = FALSE;
    GST_OBJECT_UNLOCK (self);

    gst_d3d11_window_set_render_rectangle (self->window, &rect);
  } else {
    GST_OBJECT_UNLOCK (self);
  }

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
    gst_clear_object (&self->fallback_pool);
  }

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

    d3d11_params = gst_d3d11_allocation_params_new (self->device,
        &self->info, (GstD3D11AllocationFlags) 0, bind_flags);

    self->fallback_pool = gst_d3d11_buffer_pool_new_with_options (self->device,
        caps, d3d11_params, 2, 0);
    gst_d3d11_allocation_params_free (d3d11_params);
  }

  if (!self->fallback_pool) {
    GST_ERROR_OBJECT (self, "Failed to configure fallback pool");
    return FALSE;
  }

  self->processor_in_use = FALSE;

  if (self->title) {
    gst_d3d11_window_set_title (self->window, self->title);
    g_clear_pointer (&self->title, g_free);
  }

  return TRUE;

  /* ERRORS */
invalid_format:
  {
    GST_DEBUG_OBJECT (self,
        "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
no_window:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
        ("Failed to open window."));
    return FALSE;
  }
no_disp_ratio:
  {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }
no_display_size:
  {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
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

  GST_DEBUG_OBJECT (self, "Start");

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), self->adapter,
          &self->device)) {
    GST_ERROR_OBJECT (sink, "Cannot create d3d11device");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_prepare_window (GstD3D11VideoSink * self)
{
  GstD3D11WindowNativeType window_type = GST_D3D11_WINDOW_NATIVE_TYPE_HWND;

  if (self->window)
    return TRUE;

  if (self->draw_on_shared_texture) {
    GST_INFO_OBJECT (self,
        "Create dummy window for rendering on shared texture");
    self->window = gst_d3d11_window_dummy_new (self->device);
    return TRUE;
  }

  if (!self->window_id)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (self));

  if (self->window_id) {
    window_type =
        gst_d3d11_window_get_native_type_from_handle (self->window_id);

    if (window_type != GST_D3D11_WINDOW_NATIVE_TYPE_NONE) {
      GST_DEBUG_OBJECT (self, "Have window handle %" G_GUINTPTR_FORMAT,
          self->window_id);
      gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (self),
          self->window_id);
    }
  }

  GST_DEBUG_OBJECT (self, "Create window (type: %s)",
      gst_d3d11_window_get_native_type_to_string (window_type));

#if GST_D3D11_WINAPI_ONLY_APP
  if (window_type != GST_D3D11_WINDOW_NATIVE_TYPE_CORE_WINDOW &&
      window_type != GST_D3D11_WINDOW_NATIVE_TYPE_SWAP_CHAIN_PANEL) {
    GST_ERROR_OBJECT (self, "Overlay handle must be set before READY state");
    return FALSE;
  }
#endif

  switch (window_type) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    case GST_D3D11_WINDOW_NATIVE_TYPE_HWND:
      self->window = gst_d3d11_window_win32_new (self->device, self->window_id);
      break;
#endif
#if GST_D3D11_WINAPI_APP
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
    GST_ERROR_OBJECT (self, "Cannot create d3d11window");
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

  g_clear_pointer (&self->title, g_free);

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  GstCaps *caps;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  guint size;
  gboolean need_pool;

  if (!self->device)
    return FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  /* the normal size of a frame */
  size = info.size;

  if (need_pool) {
    GstCapsFeatures *features;
    GstStructure *config;
    gboolean is_d3d11 = false;

    features = gst_caps_get_features (caps, 0);
    if (features
        && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
      GST_DEBUG_OBJECT (self, "upstream support d3d11 memory");
      pool = gst_d3d11_buffer_pool_new (self->device);
      is_d3d11 = true;
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    /* d3d11 pool does not support video alignment */
    if (!is_d3d11) {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    }

    size = GST_VIDEO_INFO_SIZE (&info);
    if (is_d3d11) {
      GstD3D11AllocationParams *d3d11_params;

      d3d11_params =
          gst_d3d11_allocation_params_new (self->device,
          &info, (GstD3D11AllocationFlags) 0, D3D11_BIND_SHADER_RESOURCE);

      gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
      gst_d3d11_allocation_params_free (d3d11_params);
    }

    gst_buffer_pool_config_set_params (config, caps, (guint) size, 2, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (pool, "Couldn't set config");
      gst_object_unref (pool);

      return FALSE;
    }

    if (is_d3d11) {
      /* d3d11 buffer pool will update buffer size based on allocated texture,
       * get size from config again */
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
          nullptr);
      gst_structure_free (config);

      /* In case of system memory, we will upload video frame to GPU memory,
       * (which is copy in any case), so crop meta support for system memory
       * is almost pointless */
      gst_query_add_allocation_meta (query,
          GST_VIDEO_CROP_META_API_TYPE, nullptr);
    }
  }

  /* We need at least 2 buffers because we hold on to the last one for redrawing
   * on window-resize event */
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
gst_d3d11_video_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *taglist;
      gchar *title = nullptr;

      gst_event_parse_tag (event, &taglist);
      gst_tag_list_get_string (taglist, GST_TAG_TITLE, &title);

      if (title) {
        const gchar *app_name = g_get_application_name ();
        std::string title_string;

        if (app_name) {
          title_string = std::string (title) + " : " + std::string (app_name);
        } else {
          title_string = std::string (title);
        }

        if (self->window) {
          gst_d3d11_window_set_title (self->window, title_string.c_str ());
        } else {
          g_free (self->title);
          self->title = g_strdup (title_string.c_str ());
        }

        g_free (title);
      }
      break;
    }
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

static gboolean
gst_d3d11_video_sink_upload_frame (GstD3D11VideoSink * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  GST_LOG_OBJECT (self, "Copy to fallback buffer");

  if (!gst_video_frame_map (&in_frame, &self->info, inbuf,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, &self->info, outbuf,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

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
  GST_LOG_OBJECT (self, "Copy to fallback buffer using device memory copy");

  return gst_d3d11_buffer_copy_into (outbuf, inbuf, &self->info);
}

static gboolean
gst_d3d11_video_sink_get_fallback_buffer (GstD3D11VideoSink * self,
    GstBuffer * inbuf, GstBuffer ** fallback_buf, gboolean device_copy)
{
  GstBuffer *outbuf = NULL;
  ID3D11ShaderResourceView *view[GST_VIDEO_MAX_PLANES];
  GstVideoOverlayCompositionMeta *compo_meta;
  GstVideoCropMeta *crop_meta;

  if (!self->fallback_pool ||
      !gst_buffer_pool_set_active (self->fallback_pool, TRUE) ||
      gst_buffer_pool_acquire_buffer (self->fallback_pool, &outbuf,
          NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "fallback pool is unavailable");
    return FALSE;
  }

  /* Ensure SRV */
  if (!gst_d3d11_buffer_get_shader_resource_view (outbuf, view)) {
    GST_ERROR_OBJECT (self, "fallback SRV is unavailable");
    goto error;
  }

  if (device_copy) {
    if (!gst_d3d11_video_sink_copy_d3d11_to_d3d11 (self, inbuf, outbuf)) {
      GST_ERROR_OBJECT (self, "cannot copy frame");
      goto error;
    }
  } else if (!gst_d3d11_video_sink_upload_frame (self, inbuf, outbuf)) {
    GST_ERROR_OBJECT (self, "cannot upload frame");
    goto error;
  }

  /* Copy overlaycomposition meta if any */
  compo_meta = gst_buffer_get_video_overlay_composition_meta (inbuf);
  if (compo_meta)
    gst_buffer_add_video_overlay_composition_meta (outbuf, compo_meta->overlay);

  /* And copy crop meta as well */
  crop_meta = gst_buffer_get_video_crop_meta (inbuf);
  if (crop_meta) {
    GstVideoCropMeta *new_crop_meta = gst_buffer_add_video_crop_meta (outbuf);

    new_crop_meta->x = crop_meta->x;
    new_crop_meta->y = crop_meta->y;
    new_crop_meta->width = crop_meta->width;
    new_crop_meta->height = crop_meta->height;
  }

  *fallback_buf = outbuf;

  return TRUE;

error:
  gst_buffer_unref (outbuf);
  return FALSE;
}

static void
gst_d3d11_video_sink_check_device_update (GstD3D11VideoSink * self,
    GstBuffer * buf)
{
  GstMemory *mem;
  GstD3D11Memory *dmem;
  gboolean update_device = FALSE;

  /* We have configured window already, cannot update device */
  if (self->window)
    return;

  mem = gst_buffer_peek_memory (buf, 0);
  if (!gst_is_d3d11_memory (mem))
    return;

  dmem = GST_D3D11_MEMORY_CAST (mem);
  /* Same device, nothing to do */
  if (dmem->device == self->device)
    return;

  if (self->adapter < 0) {
    update_device = TRUE;
  } else {
    guint adapter = 0;

    g_object_get (dmem->device, "adapter", &adapter, NULL);
    /* The same GPU as what user wanted, update */
    if (adapter == (guint) self->adapter)
      update_device = TRUE;
  }

  if (!update_device)
    return;

  GST_INFO_OBJECT (self, "Updating device %" GST_PTR_FORMAT " -> %"
      GST_PTR_FORMAT, self->device, dmem->device);

  gst_object_unref (self->device);
  self->device = (GstD3D11Device *) gst_object_ref (dmem->device);
}

static GstFlowReturn
gst_d3d11_video_sink_show_frame (GstVideoSink * sink, GstBuffer * buf)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *fallback_buf = NULL;
  ID3D11Device *device_handle =
      gst_d3d11_device_get_device_handle (self->device);
  ID3D11ShaderResourceView *view[GST_VIDEO_MAX_PLANES];

  gst_d3d11_video_sink_check_device_update (self, buf);

  if (self->caps_updated || !self->window) {
    GstCaps *caps = gst_pad_get_current_caps (GST_BASE_SINK_PAD (sink));
    gboolean update_ret;

    /* shouldn't happen */
    if (!caps)
      return GST_FLOW_NOT_NEGOTIATED;

    update_ret = gst_d3d11_video_sink_update_window (self, caps);
    gst_caps_unref (caps);

    if (!update_ret)
      return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!gst_d3d11_buffer_can_access_device (buf, device_handle)) {
    GST_LOG_OBJECT (self, "Need fallback buffer");

    if (!gst_d3d11_video_sink_get_fallback_buffer (self, buf, &fallback_buf,
            FALSE)) {
      return GST_FLOW_ERROR;
    }
  } else {
    gboolean direct_rendering = FALSE;

    /* Check if we can use video processor for conversion */
    if (gst_buffer_n_memory (buf) == 1 && self->have_video_processor) {
      GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buf, 0);
      D3D11_TEXTURE2D_DESC desc;

      gst_d3d11_memory_get_texture_desc (mem, &desc);
      if ((desc.BindFlags & D3D11_BIND_DECODER) == D3D11_BIND_DECODER) {
        GST_TRACE_OBJECT (self,
            "Got VideoProcessor compatible texture, do direct rendering");
        direct_rendering = TRUE;
        self->processor_in_use = TRUE;
      } else if (self->processor_in_use &&
          (desc.BindFlags & D3D11_BIND_RENDER_TARGET) ==
          D3D11_BIND_RENDER_TARGET) {
        direct_rendering = TRUE;
      }
    }

    /* Or, SRV should be available */
    if (!direct_rendering) {
      if (gst_d3d11_buffer_get_shader_resource_view (buf, view)) {
        GST_TRACE_OBJECT (self, "SRV is available, do direct rendering");
        direct_rendering = TRUE;
      }
    }

    if (!direct_rendering &&
        !gst_d3d11_video_sink_get_fallback_buffer (self, buf, &fallback_buf,
            TRUE)) {
      return GST_FLOW_ERROR;
    }
  }

  gst_d3d11_window_show (self->window);

  if (self->draw_on_shared_texture) {
    g_rec_mutex_lock (&self->draw_lock);
    self->current_buffer = fallback_buf ? fallback_buf : buf;
    self->drawing = TRUE;

    GST_LOG_OBJECT (self, "Begin drawing");

    /* Application should call draw method on this callback */
    g_signal_emit (self, gst_d3d11_video_sink_signals[SIGNAL_BEGIN_DRAW], 0,
        NULL);

    GST_LOG_OBJECT (self, "End drawing");
    self->drawing = FALSE;
    self->current_buffer = NULL;
    g_rec_mutex_unlock (&self->draw_lock);
  } else {
    ret = gst_d3d11_window_render (self->window,
        fallback_buf ? fallback_buf : buf);
  }

  gst_clear_buffer (&fallback_buf);

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
    GstVideoRectangle rect;

    rect.x = x;
    rect.y = y;
    rect.w = width;
    rect.h = height;

    self->render_rect = rect;
    GST_OBJECT_UNLOCK (self);

    gst_d3d11_window_set_render_rectangle (self->window, &rect);
  } else {
    self->render_rect.x = x;
    self->render_rect.y = y;
    self->render_rect.w = width;
    self->render_rect.h = height;
    self->pending_render_rect = TRUE;
    GST_OBJECT_UNLOCK (self);
  }
}

static void
gst_d3d11_video_sink_expose (GstVideoOverlay * overlay)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (overlay);

  if (self->window && self->window->swap_chain) {
    gst_d3d11_window_render (self->window, NULL);
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
  GstEvent *event = gst_event_new_navigation (structure);

  /* TODO: add support for translating native coordinate and video coordinate
   * when force-aspect-ratio is set */
  if (event) {
    gboolean handled;

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

static gboolean
gst_d3d11_video_sink_draw_action (GstD3D11VideoSink * self,
    gpointer shared_handle, guint texture_misc_flags,
    guint64 acquire_key, guint64 release_key)
{
  GstFlowReturn ret;
  g_return_val_if_fail (shared_handle != NULL, FALSE);

  if (!self->draw_on_shared_texture) {
    GST_ERROR_OBJECT (self, "Invalid draw call, we are drawing on window");
    return FALSE;
  }

  if (!shared_handle) {
    GST_ERROR_OBJECT (self, "Invalid handle");
    return FALSE;
  }

  g_rec_mutex_lock (&self->draw_lock);
  if (!self->drawing || !self->current_buffer) {
    GST_WARNING_OBJECT (self, "Nothing to draw");
    g_rec_mutex_unlock (&self->draw_lock);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "Drawing on shared handle %p, MiscFlags: 0x%x"
      ", acquire key: %" G_GUINT64_FORMAT ", release key: %"
      G_GUINT64_FORMAT, shared_handle, texture_misc_flags, acquire_key,
      release_key);

  ret = gst_d3d11_window_render_on_shared_handle (self->window,
      self->current_buffer, shared_handle, texture_misc_flags, acquire_key,
      release_key);
  g_rec_mutex_unlock (&self->draw_lock);

  return ret == GST_FLOW_OK;
}
