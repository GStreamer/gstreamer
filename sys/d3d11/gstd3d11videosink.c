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

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS
};

#define DEFAULT_ADAPTER                   -1
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE
#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { BGRA, RGBA, RGB10A2_LE }, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

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

  videosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_d3d11_video_sink_show_frame);
}

static void
gst_d3d11_video_sink_init (GstD3D11VideoSink * self)
{
  self->adapter = DEFAULT_ADAPTER;
  self->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  self->enable_navigation_events = DEFAULT_ENABLE_NAVIGATION_EVENTS;
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_video_sink_set_context (GstElement * element, GstContext * context)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (element);

  gst_d3d11_handle_set_context (element, context, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static GstCaps *
gst_d3d11_video_sink_get_caps (GstBaseSink * sink, GstCaps * filter)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  GstCaps *caps = NULL;

  if (self->device)
    caps = gst_d3d11_device_get_supported_caps (self->device,
        D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DISPLAY |
        D3D11_FORMAT_SUPPORT_RENDER_TARGET);

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
  GstCaps *sink_caps = NULL;
  gint video_width, video_height;
  gint video_par_n, video_par_d;        /* video's PAR */
  gint display_par_n = 1, display_par_d = 1;    /* display's PAR */
  guint num, den;
  D3D11_TEXTURE2D_DESC desc = { 0, };
  ID3D11Texture2D *staging;
  GError *error = NULL;

  GST_DEBUG_OBJECT (self, "set caps %" GST_PTR_FORMAT, caps);

  sink_caps = gst_d3d11_device_get_supported_caps (self->device,
      D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DISPLAY |
      D3D11_FORMAT_SUPPORT_RENDER_TARGET);

  GST_DEBUG_OBJECT (self, "supported caps %" GST_PTR_FORMAT, sink_caps);

  if (!gst_caps_can_intersect (sink_caps, caps))
    goto incompatible_caps;

  gst_clear_caps (&sink_caps);

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

  self->dxgi_format =
      gst_d3d11_dxgi_format_from_gst (GST_VIDEO_INFO_FORMAT (&self->info));

  if (!self->window_id)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (self));

  if (self->window_id) {
    GST_DEBUG_OBJECT (self, "Set external window %" G_GUINTPTR_FORMAT,
        (guintptr) self->window_id);
    gst_d3d11_window_set_window_handle (self->window, self->window_id);
  }

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

  if (!self->force_aspect_ratio) {
    g_object_set (self->window,
        "force-aspect-ratio", self->force_aspect_ratio, NULL);
  }

  GST_OBJECT_UNLOCK (self);

  if (!gst_d3d11_window_prepare (self->window, GST_VIDEO_SINK_WIDTH (self),
          GST_VIDEO_SINK_HEIGHT (self), video_par_n, video_par_d,
          self->dxgi_format, caps, &error)) {
    GstMessage *error_msg;

    GST_ERROR_OBJECT (self, "cannot create swapchain");
    error_msg = gst_message_new_error (GST_OBJECT_CAST (self),
        error, "Failed to prepare d3d11window");
    g_clear_error (&error);
    gst_element_post_message (GST_ELEMENT (self), error_msg);

    return FALSE;
  }

  if (self->fallback_staging) {
    gst_d3d11_device_release_texture (self->device, self->fallback_staging);
    self->fallback_staging = NULL;
  }

  desc.Width = GST_VIDEO_SINK_WIDTH (self);
  desc.Height = GST_VIDEO_SINK_HEIGHT (self);
  desc.MipLevels = 1;
  desc.Format = self->dxgi_format;
  desc.SampleDesc.Count = 1;
  desc.ArraySize = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);

  staging = gst_d3d11_device_create_texture (self->device, &desc, NULL);
  if (!staging) {
    GST_ERROR_OBJECT (self, "cannot create fallback staging texture");
    return FALSE;
  }

  self->fallback_staging = staging;

  return TRUE;

  /* ERRORS */
incompatible_caps:
  {
    GST_ERROR_OBJECT (sink, "caps incompatible");
    gst_clear_caps (&sink_caps);
    return FALSE;
  }
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

static void
gst_d3d11_video_sink_got_window_handle (GstD3D11Window * window,
    gpointer window_handle, GstD3D11VideoSink * self)
{
  GST_LOG_OBJECT (self,
      "got window handle %" G_GUINTPTR_FORMAT, (guintptr) window_handle);
  gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (self),
      (guintptr) window_handle);
}

static gboolean
gst_d3d11_video_sink_start (GstBaseSink * sink)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);

  GST_DEBUG_OBJECT (self, "Start");

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), &self->device,
          self->adapter) || !self->device) {
    GST_ERROR_OBJECT (sink, "Cannot create d3d11device");
    return FALSE;
  }

  self->window = gst_d3d11_window_new (self->device);
  if (!self->window) {
    GST_ERROR_OBJECT (sink, "Cannot create d3d11window");
    return FALSE;
  }

  g_object_set (self->window,
      "enable-navigation-events", self->enable_navigation_events, NULL);

  g_signal_connect (self->window, "key-event",
      G_CALLBACK (gst_d3d11_video_sink_key_event), self);
  g_signal_connect (self->window, "mouse-event",
      G_CALLBACK (gst_d3d11_video_mouse_key_event), self);
  g_signal_connect (self->window, "got-window-handle",
      G_CALLBACK (gst_d3d11_video_sink_got_window_handle), self);

  return TRUE;
}

static gboolean
gst_d3d11_video_sink_stop (GstBaseSink * sink)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);

  GST_DEBUG_OBJECT (self, "Stop");

  if (self->fallback_staging) {
    ID3D11Texture2D_Release (self->fallback_staging);
    self->fallback_staging = NULL;
  }

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
    GST_DEBUG_OBJECT (self, "create new pool");

    pool = (GstBufferPool *) gst_d3d11_buffer_pool_new (self->device);
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 2,
        DXGI_MAX_SWAP_CHAIN_BUFFERS);

    if (!gst_buffer_pool_set_config (pool, config)) {
      g_object_unref (pool);
      goto config_failed;
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 2,
      DXGI_MAX_SWAP_CHAIN_BUFFERS);
  if (pool)
    g_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

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

typedef struct
{
  GstD3D11VideoSink *sink;

  GstVideoFrame *frame;
  ID3D11Resource *resource;

  GstFlowReturn ret;
} FrameUploadData;

static void
_upload_frame (GstD3D11Device * device, gpointer data)
{
  GstD3D11VideoSink *self;
  HRESULT hr;
  ID3D11DeviceContext *device_context;
  FrameUploadData *upload_data = (FrameUploadData *) data;
  D3D11_MAPPED_SUBRESOURCE d3d11_map;
  guint i;
  guint8 *dst;

  self = upload_data->sink;

  device_context = gst_d3d11_device_get_device_context (device);

  hr = ID3D11DeviceContext_Map (device_context,
      upload_data->resource, 0, D3D11_MAP_WRITE, 0, &d3d11_map);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "cannot map d3d11 staging texture");
    upload_data->ret = GST_FLOW_ERROR;
    return;
  }

  dst = d3d11_map.pData;
  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (upload_data->frame); i++) {
    guint w, h;
    guint j;
    guint8 *src;
    gint src_stride;

    w = GST_VIDEO_FRAME_COMP_WIDTH (upload_data->frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (upload_data->frame, i);
    h = GST_VIDEO_FRAME_COMP_HEIGHT (upload_data->frame, i);
    src = GST_VIDEO_FRAME_PLANE_DATA (upload_data->frame, i);
    src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (upload_data->frame, i);

    for (j = 0; j < h; j++) {
      memcpy (dst, src, w);
      dst += d3d11_map.RowPitch;
      src += src_stride;
    }
  }

  ID3D11DeviceContext_Unmap (device_context, upload_data->resource, 0);
}

static GstFlowReturn
gst_d3d11_video_sink_show_frame (GstVideoSink * sink, GstBuffer * buf)
{
  GstD3D11VideoSink *self = GST_D3D11_VIDEO_SINK (sink);
  GstVideoFrame frame;
  FrameUploadData data;
  ID3D11Texture2D *texture;
  GstMapInfo map;
  GstFlowReturn ret;
  gboolean need_unmap = FALSE;
  GstMemory *mem;
  GstVideoRectangle rect = { 0, };
  GstVideoCropMeta *crop;

  if (gst_buffer_n_memory (buf) == 1 && (mem = gst_buffer_peek_memory (buf, 0))
      && gst_memory_is_type (mem, GST_D3D11_MEMORY_NAME)) {
    /* If this buffer has been allocated using our buffer management we simply
       put the ximage which is in the PRIVATE pointer */
    GST_TRACE_OBJECT (self, "buffer %p from our pool, writing directly", buf);
    if (!gst_memory_map (mem, &map, (GST_MAP_READ | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "cannot map d3d11 memory");
      return GST_FLOW_ERROR;
    }

    texture = (ID3D11Texture2D *) map.data;
    need_unmap = TRUE;
  } else {
    if (!gst_video_frame_map (&frame, &self->info, buf, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "cannot map video frame");
      return GST_FLOW_ERROR;
    }

    GST_TRACE_OBJECT (self,
        "buffer %p out of our pool, write to stage buffer", buf);

    data.sink = self;
    data.frame = &frame;
    data.resource = (ID3D11Resource *) self->fallback_staging;
    data.ret = GST_FLOW_OK;

    gst_d3d11_device_thread_add (self->device, (GstD3D11DeviceThreadFunc)
        _upload_frame, &data);

    if (data.ret != GST_FLOW_OK)
      return data.ret;

    gst_video_frame_unmap (&frame);

    texture = self->fallback_staging;
  }

  gst_d3d11_window_show (self->window);

  crop = gst_buffer_get_video_crop_meta (buf);
  if (crop) {
    rect.x = crop->x;
    rect.y = crop->y;
    rect.w = crop->width;
    rect.h = crop->height;
  } else {
    rect.w = self->video_width;
    rect.h = self->video_height;
  }

  ret = gst_d3d11_window_render (self->window, texture, &rect);

  if (need_unmap)
    gst_memory_unmap (mem, &map);

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
