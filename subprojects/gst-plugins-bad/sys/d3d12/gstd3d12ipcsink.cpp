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
 * SECTION:element-d3d12ipcsink
 * @title: d3d12ipcsink
 * @short_description: Direct3D12 Inter Process Communication (IPC) sink
 *
 * d3d12ipcsink exports Direct3D12 texture for connected d3d12ipcsrc elements
 * to be able to import it
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! d3d12upload ! d3d12ipcsink
 * ```
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12ipcsink.h"
#include "gstd3d12ipcserver.h"
#include "gstd3d12pluginutils.h"
#include <mutex>
#include <string>
#include <vector>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_ipc_sink_debug);
#define GST_CAT_DEFAULT gst_d3d12_ipc_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_IPC_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE (GST_D3D12_IPC_FORMATS)));

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_PIPE_NAME,
};

#define DEFAULT_ADAPTER -1
#define DEFAULT_PIPE_NAME "\\\\.\\pipe\\gst.d3d12.ipc"

/* *INDENT-OFF* */
struct GstD3D12IpcSinkPrivate
{
  GstD3D12Device *device = nullptr;

  GstBufferPool *fallback_pool = nullptr;
  GstVideoInfo info;

  GstD3D12IpcServer *server = nullptr;
  GstCaps *caps = nullptr;
  GstSample *prepared_sample = nullptr;
  HANDLE prepared_handle = nullptr;
  GstD3D12IpcMemLayout layout;

  std::mutex lock;

  /* properties */
  gint adapter = DEFAULT_ADAPTER;
  std::string pipe_name = DEFAULT_PIPE_NAME;
};
/* *INDENT-ON* */

struct _GstD3D12IpcSink
{
  GstBaseSink parent;

  GstD3D12IpcSinkPrivate *priv;
};

static void gst_d3d12_ipc_sink_finalize (GObject * object);
static void gst_d3d12_ipc_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_d3d12_ipc_sink_provide_clock (GstElement * elem);
static void gst_d3d12_ipc_sink_set_context (GstElement * elem,
    GstContext * context);

static gboolean gst_d3d12_ipc_sink_start (GstBaseSink * sink);
static gboolean gst_d3d12_ipc_sink_stop (GstBaseSink * sink);
static gboolean gst_d3d12_ipc_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static void gst_d3d12_ipc_sink_get_time (GstBaseSink * sink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_d3d12_ipc_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query);
static gboolean gst_d3d12_ipc_sink_query (GstBaseSink * sink, GstQuery * query);
static GstFlowReturn gst_d3d12_ipc_sink_prepare (GstBaseSink * sink,
    GstBuffer * buf);
static GstFlowReturn gst_d3d12_ipc_sink_render (GstBaseSink * sink,
    GstBuffer * buf);

#define gst_d3d12_ipc_sink_parent_class parent_class
G_DEFINE_TYPE (GstD3D12IpcSink, gst_d3d12_ipc_sink, GST_TYPE_BASE_SINK);

static void
gst_d3d12_ipc_sink_class_init (GstD3D12IpcSinkClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto sink_class = GST_BASE_SINK_CLASS (klass);

  object_class->finalize = gst_d3d12_ipc_sink_finalize;
  object_class->set_property = gst_d3d12_ipc_sink_set_property;
  object_class->get_property = gst_win32_video_sink_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "DXGI adapter index (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_PIPE_NAME,
      g_param_spec_string ("pipe-name", "Pipe Name",
          "The name of Win32 named pipe to communicate with clients. "
          "Validation of the pipe name is caller's responsibility",
          DEFAULT_PIPE_NAME, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 IPC Sink", "Sink/Video",
      "Sends Direct3D12 shared handle to peer d3d12ipcsrc elements",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_provide_clock);
  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_set_context);

  sink_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_start);
  sink_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_stop);
  sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_set_caps);
  sink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_propose_allocation);
  sink_class->query = GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_query);
  sink_class->get_times = GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_get_time);
  sink_class->prepare = GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_prepare);
  sink_class->render = GST_DEBUG_FUNCPTR (gst_d3d12_ipc_sink_render);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_ipc_sink_debug, "d3d12ipcsink",
      0, "d3d12ipcsink");
}

static void
gst_d3d12_ipc_sink_init (GstD3D12IpcSink * self)
{
  self->priv = new GstD3D12IpcSinkPrivate ();

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_d3d12_ipc_sink_finalize (GObject * object)
{
  auto self = GST_D3D12_IPC_SINK (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_ipc_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_IPC_SINK (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_ADAPTER:
      priv->adapter = g_value_get_int (value);
      break;
    case PROP_PIPE_NAME:
    {
      const gchar *pipe_name = g_value_get_string (value);
      priv->pipe_name.clear ();

      if (pipe_name)
        priv->pipe_name = pipe_name;
      else
        priv->pipe_name = DEFAULT_PIPE_NAME;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win32_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_IPC_SINK (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter);
      break;
    case PROP_PIPE_NAME:
      g_value_set_string (value, priv->pipe_name.c_str ());
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_d3d12_ipc_sink_provide_clock (GstElement * elem)
{
  return gst_system_clock_obtain ();
}

static void
gst_d3d12_ipc_sink_set_context (GstElement * elem, GstContext * context)
{
  auto self = GST_D3D12_IPC_SINK (elem);
  auto priv = self->priv;

  gst_d3d12_handle_set_context (elem, context, priv->adapter, &priv->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (elem, context);
}

static gboolean
gst_d3d12_ipc_sink_start (GstBaseSink * sink)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  if (!gst_d3d12_ensure_element_data (GST_ELEMENT_CAST (self), priv->adapter,
          &priv->device)) {
    GST_ERROR_OBJECT (sink, "Cannot create d3d12device");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_ipc_sink_stop (GstBaseSink * sink)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  if (priv->server)
    gst_d3d12_ipc_server_stop (priv->server);
  gst_clear_object (&priv->server);

  GST_DEBUG_OBJECT (self, "Server cleared");

  if (priv->fallback_pool) {
    gst_buffer_pool_set_active (priv->fallback_pool, FALSE);
    gst_clear_object (&priv->fallback_pool);
  }

  gst_clear_sample (&priv->prepared_sample);
  gst_clear_object (&priv->device);

  return TRUE;
}

static void
gst_d3d12_ipc_sink_get_time (GstBaseSink * sink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  auto priv = self->priv;
  GstClockTime timestamp;

  timestamp = GST_BUFFER_PTS (buf);
  if (!GST_CLOCK_TIME_IS_VALID (timestamp))
    timestamp = GST_BUFFER_DTS (buf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    *start = timestamp;
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = timestamp + GST_BUFFER_DURATION (buf);
    } else if (priv->info.fps_n > 0) {
      *end = timestamp +
          gst_util_uint64_scale_int (GST_SECOND, priv->info.fps_d,
          priv->info.fps_n);
    } else if (sink->segment.rate < 0) {
      *end = timestamp;
    }
  }
}

static GstBufferPool *
gst_d3d12_ipc_sink_create_pool (GstD3D12IpcSink * self,
    const GstVideoInfo * info, GstCaps * caps)
{
  auto priv = self->priv;

  auto pool = gst_d3d12_buffer_pool_new (priv->device);
  auto config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, priv->caps,
      GST_VIDEO_INFO_SIZE (info), 0, 0);

  auto params = gst_d3d12_allocation_params_new (priv->device, &priv->info,
      GST_D3D12_ALLOCATION_FLAG_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_HEAP_FLAG_SHARED);

  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    gst_object_unref (pool);
    return nullptr;
  }

  return pool;
}

static gboolean
gst_d3d12_ipc_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  auto priv = self->priv;
  GstCaps *new_caps;
  GstStructure *s;
  const gchar *str;

  GST_DEBUG_OBJECT (self, "New caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&priv->info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (priv->fallback_pool) {
    gst_buffer_pool_set_active (priv->fallback_pool, FALSE);
    gst_clear_object (&priv->fallback_pool);
  }

  s = gst_caps_get_structure (caps, 0);

  /* Takes values we know it's always deserializable */
  new_caps = gst_caps_new_empty_simple ("video/x-raw");
  gst_caps_set_simple (new_caps, "format", G_TYPE_STRING,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&priv->info)),
      "width", G_TYPE_INT, priv->info.width,
      "height", G_TYPE_INT, priv->info.height,
      "framerate", GST_TYPE_FRACTION, priv->info.fps_n, priv->info.fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, priv->info.par_n,
      priv->info.par_d, nullptr);

  str = gst_structure_get_string (s, "colorimetry");
  if (str)
    gst_caps_set_simple (new_caps, "colorimetry", G_TYPE_STRING, str, nullptr);

  str = gst_structure_get_string (s, "mastering-display-info");
  if (str) {
    gst_caps_set_simple (new_caps, "mastering-display-info", G_TYPE_STRING,
        str, nullptr);
  }

  str = gst_structure_get_string (s, "content-light-level");
  if (str) {
    gst_caps_set_simple (new_caps, "content-light-level", G_TYPE_STRING,
        str, nullptr);
  }

  gst_caps_set_features_simple (new_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, nullptr));

  gst_clear_caps (&priv->caps);
  priv->caps = new_caps;

  return TRUE;
}

static gboolean
gst_d3d12_ipc_sink_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  GstCaps *caps;
  GstBufferPool *pool = nullptr;
  GstVideoInfo info;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_WARNING_OBJECT (sink, "No caps specified");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (sink, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  /* the normal size of a frame */
  size = info.size;
  if (need_pool) {
    auto features = gst_caps_get_features (caps, 0);
    if (features
        && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
      GST_DEBUG_OBJECT (self, "upstream support d3d12 memory");
      pool = gst_d3d12_ipc_sink_create_pool (self, &info, caps);
      if (!pool) {
        GST_ERROR_OBJECT (self, "Couldn't create pool");
        return FALSE;
      }

      auto config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
          nullptr);
      gst_structure_free (config);
    } else {
      pool = gst_video_buffer_pool_new ();
      auto config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_META);
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

      gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

      if (!gst_buffer_pool_set_config (pool, config)) {
        GST_ERROR_OBJECT (pool, "Couldn't set config");
        gst_object_unref (pool);

        return FALSE;
      }
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 0, 0);
  gst_clear_object (&pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_d3d12_ipc_sink_query (GstBaseSink * sink, GstQuery * query)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d12_handle_context_query (GST_ELEMENT (self), query,
              priv->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->query (sink, query);
}

static GstBuffer *
gst_d3d12_ipc_upload (GstD3D12IpcSink * self, GstBuffer * buf)
{
  auto priv = self->priv;
  GstBuffer *uploaded = nullptr;
  GstFlowReturn ret;
  GstMemory *mem;

  mem = gst_buffer_peek_memory (buf, 0);
  if (gst_is_d3d12_memory (mem)) {
    auto dmem = GST_D3D12_MEMORY_CAST (mem);
    if (gst_d3d12_device_is_equal (dmem->device, priv->device)) {
      D3D12_RESOURCE_DESC desc;
      D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;

      auto resource = gst_d3d12_memory_get_resource_handle (dmem);
      desc = GetDesc (resource);
      resource->GetHeapProperties (nullptr, &heap_flags);
      if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0 &&
          (heap_flags & D3D12_HEAP_FLAG_SHARED) != 0) {
        return gst_buffer_ref (buf);
      }
    }
  }

  if (!priv->fallback_pool) {
    priv->fallback_pool = gst_d3d12_ipc_sink_create_pool (self, &priv->info,
        priv->caps);
    if (!priv->fallback_pool) {
      GST_ERROR_OBJECT (self, "Couldn't create fallback pool");
      return nullptr;
    }

    if (!gst_buffer_pool_set_active (priv->fallback_pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Couldn't active pool");
      gst_clear_object (&priv->fallback_pool);
      return nullptr;
    }
  }

  ret = gst_buffer_pool_acquire_buffer (priv->fallback_pool,
      &uploaded, nullptr);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't acquire fallback buffer");
    return nullptr;
  }

  if (!gst_d3d12_buffer_copy_into (uploaded, buf, &priv->info)) {
    GST_ERROR_OBJECT (self, "Couldn't copy buffer");
    gst_buffer_unref (uploaded);
    return nullptr;
  }

  return uploaded;
}

static gboolean
gst_d3d12_ipc_sink_ensure_server (GstD3D12IpcSink * self, GstBuffer * buffer)
{
  auto priv = self->priv;
  GstMemory *mem;
  gint64 adapter_luid;

  if (priv->server)
    return TRUE;

  g_object_get (priv->device, "adapter-luid", &adapter_luid, nullptr);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (gst_is_d3d12_memory (mem)) {
    GstD3D12Memory *dmem = GST_D3D12_MEMORY_CAST (mem);
    if (!gst_d3d12_device_is_equal (dmem->device, priv->device)) {
      g_object_get (dmem->device, "adapter-luid", &adapter_luid, nullptr);
      gst_object_unref (priv->device);
      priv->device = (GstD3D12Device *) gst_object_ref (dmem->device);
    }
  }

  auto fence = gst_d3d12_device_get_fence_handle (priv->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  priv->server = gst_d3d12_ipc_server_new (priv->pipe_name, adapter_luid,
      fence);
  if (!priv->server) {
    GST_ERROR_OBJECT (self, "Couldn't create server");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_d3d12_ipc_sink_prepare (GstBaseSink * sink, GstBuffer * buf)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  auto priv = self->priv;
  GstBuffer *uploaded;
  GstD3D12Memory *dmem;
  GstVideoFrame frame;
  HANDLE nt_handle = nullptr;

  gst_clear_sample (&priv->prepared_sample);

  if (!gst_d3d12_ipc_sink_ensure_server (self, buf))
    return GST_FLOW_ERROR;

  uploaded = gst_d3d12_ipc_upload (self, buf);
  if (!uploaded) {
    GST_ERROR_OBJECT (self, "Couldn't upload buffer");
    return GST_FLOW_ERROR;
  }

  dmem = (GstD3D12Memory *) gst_buffer_peek_memory (uploaded, 0);

  /* Upload staging to device memory */
  if (!gst_video_frame_map (&frame, &priv->info, uploaded, GST_MAP_READ_D3D12)) {
    GST_ERROR_OBJECT (self, "Couldn't upload memory");
    gst_buffer_unref (uploaded);
    return GST_FLOW_ERROR;
  }

  priv->layout.pitch = frame.info.stride[0];
  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&frame); i++)
    priv->layout.offset[i] = frame.info.offset[i];

  gst_video_frame_unmap (&frame);

  GstD3D12Frame d3d12_frame;
  if (!gst_d3d12_frame_map (&d3d12_frame, &priv->info, uploaded, GST_MAP_D3D12,
          GST_D3D12_FRAME_MAP_FLAG_NONE)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame");
    gst_buffer_unref (uploaded);
    return GST_FLOW_ERROR;
  }

  gst_d3d12_frame_fence_cpu_wait (&d3d12_frame);
  gst_d3d12_frame_unmap (&d3d12_frame);

  if (!gst_d3d12_memory_get_nt_handle (dmem, &nt_handle)) {
    GST_ERROR_OBJECT (self, "Couldn't get NT handle");
    gst_buffer_unref (uploaded);
    return GST_FLOW_ERROR;
  }

  priv->prepared_sample = gst_sample_new (uploaded,
      priv->caps, nullptr, nullptr);
  priv->prepared_handle = nt_handle;

  gst_buffer_unref (uploaded);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_ipc_sink_render (GstBaseSink * sink, GstBuffer * buf)
{
  auto self = GST_D3D12_IPC_SINK (sink);
  auto priv = self->priv;
  GstClockTime pts;
  GstClockTime now_system;
  GstClockTime buf_pts;
  GstClockTime buffer_clock = GST_CLOCK_TIME_NONE;
  GstFlowReturn ret;

  if (!priv->prepared_sample) {
    GST_ERROR_OBJECT (self, "Have no prepared sample");
    return GST_FLOW_ERROR;
  }

  pts = now_system = gst_util_get_timestamp ();
  buf_pts = GST_BUFFER_PTS (buf);
  if (!GST_CLOCK_TIME_IS_VALID (buf_pts))
    buf_pts = GST_BUFFER_DTS (buf);

  if (GST_CLOCK_TIME_IS_VALID (buf_pts)) {
    buffer_clock = gst_segment_to_running_time (&sink->segment,
        GST_FORMAT_TIME, buf_pts) +
        GST_ELEMENT_CAST (sink)->base_time + gst_base_sink_get_latency (sink);
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer_clock)) {
    GstClock *clock = gst_element_get_clock (GST_ELEMENT_CAST (sink));
    if (!gst_d3d12_ipc_clock_is_system (clock)) {
      GstClockTime now_gst = gst_clock_get_time (clock);
      GstClockTimeDiff converted = buffer_clock;

      converted -= now_gst;
      converted += now_system;

      if (converted < 0) {
        /* Shouldn't happen */
        GST_WARNING_OBJECT (self, "Negative buffer clock");
        pts = 0;
      } else {
        pts = converted;
      }
    } else {
      /* buffer clock is already system time */
      pts = buffer_clock;
    }
    gst_object_unref (clock);
  }

  ret = gst_d3d12_ipc_server_send_data (priv->server, priv->prepared_sample,
      priv->layout, priv->prepared_handle, pts);

  return ret;
}
