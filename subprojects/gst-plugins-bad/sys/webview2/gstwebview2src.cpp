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
 * SECTION:element-webview2src
 * @title: webview2src
 * @short_description: WebView2 based browser source
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwebview2src.h"
#include "gstwebview2object.h"
#include <gst/d3d11/gstd3d11-private.h>
#include <gst/d3d12/gstd3d12.h>
#include <mutex>
#include <string>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY (gst_webview2_src_debug);
#define GST_CAT_DEFAULT gst_webview2_src_debug

static GstStaticPadTemplate pad_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
            "BGRA") ", pixel-aspect-ratio = 1/1; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY,
            "BGRA") ", pixel-aspect-ratio = 1/1; "
        GST_VIDEO_CAPS_MAKE ("BGRA") ", pixel-aspect-ratio = 1/1"));

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_LOCATION,
  PROP_PROCESSING_DEADLINE,
  PROP_JAVASCRIPT,
  PROP_USER_DATA_FOLDER,
};

#define DEFAULT_LOCATION "about:blank"
#define DEFAULT_PROCESSING_DEADLINE (20 * GST_MSECOND)
#define DEFAULT_ADAPTER -1

/* *INDENT-OFF* */
struct GstWebView2SrcPrivate
{
  GstWebView2SrcPrivate ()
  {
    event_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  }

  ~GstWebView2SrcPrivate ()
  {
    ClearResource ();
    gst_clear_object (&object);
    gst_clear_object (&device12);
    gst_clear_object (&device);

    CloseHandle (event_handle);
  }

  void ClearResource ()
  {
    if (fence12) {
      auto completed = fence12->GetCompletedValue ();
      if (completed < fence_val) {
        auto hr = fence12->SetEventOnCompletion (fence_val, event_handle);
        if (SUCCEEDED (hr))
          WaitForSingleObject (event_handle, INFINITE);
      }
    }

    staging = nullptr;
    fence12 = nullptr;
    fence11 = nullptr;
    fence_val = 0;
    context4 = nullptr;
    device_5 = nullptr;
    can_d3d12_copy = FALSE;
  }

  GstD3D11Device *device = nullptr;
  GstD3D12Device *device12 = nullptr;

  GstWebView2Object *object = nullptr;

  std::mutex lock;
  GstVideoInfo info;
  guint64 last_frame_no;
  GstClockID clock_id = nullptr;
  ComPtr<ID3D11Texture2D> staging;

  /* D3D12 interop */
  ComPtr<ID3D11Device5> device_5;
  ComPtr<ID3D11DeviceContext4> context4;
  ComPtr<ID3D11Fence> fence11;
  ComPtr<ID3D12Fence> fence12;
  gboolean can_d3d12_copy;
  UINT64 fence_val = 0;
  HANDLE event_handle;

  /* properties */
  gint adapter_index = DEFAULT_ADAPTER;
  std::string location = DEFAULT_LOCATION;
  GstClockTime processing_deadline = DEFAULT_PROCESSING_DEADLINE;
  std::string script;
  std::string user_data_folder;
};
/* *INDENT-ON* */

struct _GstWebView2Src
{
  GstBaseSrc parent;

  GstWebView2SrcPrivate *priv;
};

static void gst_webview2_src_finalize (GObject * object);
static void gst_webview2_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_webview2_src_provide_clock (GstElement * elem);
static void gst_webview2_src_set_context (GstElement * elem,
    GstContext * context);

static gboolean gst_webview2_src_start (GstBaseSrc * src);
static gboolean gst_webview2_src_stop (GstBaseSrc * src);
static gboolean gst_webview2_src_unlock (GstBaseSrc * src);
static gboolean gst_webview2_src_unlock_stop (GstBaseSrc * src);
static gboolean gst_webview2_src_query (GstBaseSrc * src, GstQuery * query);
static GstCaps *gst_webview2_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_webview2_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_webview2_decide_allocation (GstBaseSrc * src,
    GstQuery * query);
static gboolean gst_webview2_src_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn gst_webview2_src_create (GstBaseSrc * src,
    guint64 offset, guint size, GstBuffer ** buf);
static void gst_webview2_src_uri_handler_init (gpointer iface, gpointer data);

#define gst_webview2_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebView2Src, gst_webview2_src, GST_TYPE_BASE_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_webview2_src_uri_handler_init));

static void
gst_webview2_src_class_init (GstWebView2SrcClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto src_class = GST_BASE_SRC_CLASS (klass);

  object_class->finalize = gst_webview2_src_finalize;
  object_class->set_property = gst_webview2_src_set_property;
  object_class->get_property = gst_win32_video_src_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "DXGI Adapter index (-1 for any device)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "The URL to display",
          nullptr, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (object_class, PROP_PROCESSING_DEADLINE,
      g_param_spec_uint64 ("processing-deadline", "Processing deadline",
          "Maximum processing time for a buffer in nanoseconds", 0, G_MAXUINT64,
          DEFAULT_PROCESSING_DEADLINE, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (object_class, PROP_JAVASCRIPT,
      g_param_spec_string ("javascript", "Javascript",
          "Javascript to run on nevigation completed",
          nullptr, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (object_class, PROP_USER_DATA_FOLDER,
      g_param_spec_string ("user-data-folder", "User Data Folder",
          "Absolute path to WebView2 user data folder location.", nullptr,
          (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  gst_element_class_set_static_metadata (element_class,
      "WebView2 Source", "Source/Video",
      "Creates a video stream rendered by WebView2",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &pad_template);

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_webview2_src_provide_clock);
  element_class->set_context = GST_DEBUG_FUNCPTR (gst_webview2_src_set_context);

  src_class->start = GST_DEBUG_FUNCPTR (gst_webview2_src_start);
  src_class->stop = GST_DEBUG_FUNCPTR (gst_webview2_src_stop);
  src_class->unlock = GST_DEBUG_FUNCPTR (gst_webview2_src_unlock);
  src_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_webview2_src_unlock_stop);
  src_class->query = GST_DEBUG_FUNCPTR (gst_webview2_src_query);
  src_class->fixate = GST_DEBUG_FUNCPTR (gst_webview2_src_fixate);
  src_class->set_caps = GST_DEBUG_FUNCPTR (gst_webview2_src_set_caps);
  src_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_webview2_decide_allocation);
  src_class->event = GST_DEBUG_FUNCPTR (gst_webview2_src_event);
  src_class->create = GST_DEBUG_FUNCPTR (gst_webview2_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_webview2_src_debug, "webview2src",
      0, "webview2src");
}

static void
gst_webview2_src_init (GstWebView2Src * self)
{
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);

  self->priv = new GstWebView2SrcPrivate ();

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_webview2_src_finalize (GObject * object)
{
  auto self = GST_WEBVIEW2_SRC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webview2_src_set_location (GstWebView2Src * self, const gchar * location)
{
  auto priv = self->priv;
  priv->location.clear ();
  if (location)
    priv->location = location;
  else
    priv->location = DEFAULT_LOCATION;

  if (priv->object) {
    gst_webview2_object_set_location (priv->object,
        priv->location, priv->script);
  }
}

static void
gst_webview2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WEBVIEW2_SRC (object);
  auto priv = self->priv;
  std::unique_lock < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_ADAPTER:
      priv->adapter_index = g_value_get_int (value);
      break;
    case PROP_LOCATION:
      gst_webview2_src_set_location (self, g_value_get_string (value));
      break;
    case PROP_PROCESSING_DEADLINE:
    {
      GstClockTime prev_val, new_val;
      prev_val = priv->processing_deadline;
      new_val = g_value_get_uint64 (value);
      priv->processing_deadline = new_val;

      if (prev_val != new_val) {
        lk.unlock ();
        GST_DEBUG_OBJECT (self, "Posting latency message");
        gst_element_post_message (GST_ELEMENT_CAST (self),
            gst_message_new_latency (GST_OBJECT_CAST (self)));
      }
      break;
    }
    case PROP_JAVASCRIPT:
    {
      auto script = g_value_get_string (value);
      if (script)
        priv->script = script;
      else
        priv->script.clear ();

      if (priv->object) {
        gst_webview2_object_set_location (priv->object, priv->location,
            priv->script);
      }
      break;
    }
    case PROP_USER_DATA_FOLDER:
    {
      auto udf = g_value_get_string (value);
      if (udf)
        priv->user_data_folder = udf;
      else
        priv->user_data_folder.clear ();
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win32_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_WEBVIEW2_SRC (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter_index);
      break;
    case PROP_LOCATION:
      g_value_set_string (value, priv->location.c_str ());
      break;
    case PROP_PROCESSING_DEADLINE:
      g_value_set_uint64 (value, priv->processing_deadline);
      break;
    case PROP_JAVASCRIPT:
      g_value_set_string (value, priv->script.c_str ());
      break;
    case PROP_USER_DATA_FOLDER:
      g_value_set_string (value, priv->script.c_str ());
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_webview2_src_provide_clock (GstElement * elem)
{
  return gst_system_clock_obtain ();
}

static void
gst_webview2_src_set_context (GstElement * elem, GstContext * context)
{
  auto self = GST_WEBVIEW2_SRC (elem);
  auto priv = self->priv;

  gst_d3d11_handle_set_context (elem,
      context, priv->adapter_index, &priv->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (elem, context);
}

static gboolean
gst_webview2_src_start (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  auto elem = GST_ELEMENT_CAST (src);
  HRESULT hr;

  GST_DEBUG_OBJECT (self, "Start");

  priv->ClearResource ();

  if (!gst_d3d11_ensure_element_data (elem, priv->adapter_index, &priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't get D3D11 device");
    return FALSE;
  }

  gint64 luid;
  g_object_get (priv->device, "adapter-luid", &luid, nullptr);
  if (!gst_d3d12_ensure_element_data_for_adapter_luid (elem,
          luid, &priv->device12)) {
    GST_ERROR_OBJECT (self, "Couldn't get d3d12 device");
    return FALSE;
  }

  auto device = gst_d3d11_device_get_device_handle (priv->device);
  device->QueryInterface (IID_PPV_ARGS (&priv->device_5));

  auto context = gst_d3d11_device_get_device_context_handle (priv->device);
  context->QueryInterface (IID_PPV_ARGS (&priv->context4));

  if (priv->device_5 && priv->context4) {
    hr = priv->device_5->CreateFence (0, D3D11_FENCE_FLAG_SHARED,
        IID_PPV_ARGS (&priv->fence11));
    if (gst_d3d11_result (hr, priv->device)) {
      HANDLE fence_handle;
      hr = priv->fence11->CreateSharedHandle (nullptr, GENERIC_ALL, nullptr,
          &fence_handle);
      if (gst_d3d11_result (hr, priv->device)) {
        auto device12 = gst_d3d12_device_get_device_handle (priv->device12);
        hr = device12->OpenSharedHandle (fence_handle,
            IID_PPV_ARGS (&priv->fence12));
        CloseHandle (fence_handle);
        if (gst_d3d12_result (hr, priv->device12))
          priv->can_d3d12_copy = TRUE;
      }
    }
  }

  GST_DEBUG_OBJECT (self, "D3D12 copy support: %d", priv->can_d3d12_copy);

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->object = gst_webview2_object_new (priv->device, priv->user_data_folder);
  if (!priv->object) {
    GST_ERROR_OBJECT (self, "Couldn't create object");
    return FALSE;
  }

  gst_webview2_object_set_location (priv->object, priv->location, priv->script);

  priv->last_frame_no = -1;

  return TRUE;
}

static gboolean
gst_webview2_src_stop (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  GST_DEBUG_OBJECT (self, "Stop");

  priv->ClearResource ();

  gst_clear_object (&priv->object);
  gst_clear_object (&priv->device);
  gst_clear_object (&priv->device12);

  return TRUE;
}

static gboolean
gst_webview2_src_unlock (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->object)
    gst_webview2_object_set_flushing (priv->object, true);

  if (priv->clock_id)
    gst_clock_id_unschedule (priv->clock_id);

  return TRUE;
}

static gboolean
gst_webview2_src_unlock_stop (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->object)
    gst_webview2_object_set_flushing (priv->object, false);

  return TRUE;
}

static gboolean
gst_webview2_src_query (GstBaseSrc * src, GstQuery * query)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      std::lock_guard < std::mutex > lk (priv->lock);
      if (GST_CLOCK_TIME_IS_VALID (priv->processing_deadline)) {
        gst_query_set_latency (query, TRUE, priv->processing_deadline,
            GST_CLOCK_TIME_NONE);
      } else {
        gst_query_set_latency (query, TRUE, 0, 0);
      }
      return TRUE;
    }
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (self), query,
              priv->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (src, query);
}

static GstCaps *
gst_webview2_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  caps = gst_caps_make_writable (caps);
  auto s = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (s, "width", 1920);
  gst_structure_fixate_field_nearest_int (s, "height", 1080);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", 30, 1);

  return GST_BASE_SRC_CLASS (parent_class)->fixate (src, caps);
}

static gboolean
gst_webview2_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (!gst_video_info_from_caps (&priv->info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);
  priv->staging = nullptr;

  if (priv->object) {
    gst_webview_object_update_size (priv->object,
        priv->info.width, priv->info.height);
  }

  return TRUE;
}

static gboolean
gst_webview2_decide_allocation (GstBaseSrc * src, GstQuery * query)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  GstCaps *caps;

  gst_query_parse_allocation (query, &caps, nullptr);
  if (!caps) {
    GST_ERROR_OBJECT (self, "No output caps");
    return FALSE;
  }

  GstVideoInfo info;
  gst_video_info_from_caps (&info, caps);

  gboolean update_pool = FALSE;
  GstBufferPool *pool = nullptr;
  guint min, max, size;
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    size = info.size;
    min = max = 0;
    update_pool = FALSE;
  }

  auto features = gst_caps_get_features (caps, 0);
  auto is_d3d11 = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);
  auto is_d3d12 = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);
  if (pool) {
    if (is_d3d11) {
      if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto d3d11_pool = GST_D3D11_BUFFER_POOL (pool);
        if (d3d11_pool->device != priv->device)
          gst_clear_object (&pool);
      }
    } else if (is_d3d12) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto d3d12_pool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (d3d12_pool->device, priv->device12))
          gst_clear_object (&pool);
      }
    }
  }

  if (!pool) {
    if (is_d3d11)
      pool = gst_d3d11_buffer_pool_new (priv->device);
    else if (is_d3d12)
      pool = gst_d3d12_buffer_pool_new (priv->device12);
    else
      pool = gst_video_buffer_pool_new ();
  }

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (is_d3d11) {
    auto params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!params) {
      params = gst_d3d11_allocation_params_new (priv->device, &info,
          GST_D3D11_ALLOCATION_FLAG_DEFAULT,
          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0);
    } else {
      gst_d3d11_allocation_params_set_bind_flags (params,
          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
    gst_d3d11_allocation_params_free (params);
  } else if (is_d3d12) {
    auto params = gst_buffer_pool_config_get_d3d12_allocation_params (config);
    if (!params) {
      params = gst_d3d12_allocation_params_new (priv->device12, &info,
          GST_D3D12_ALLOCATION_FLAG_DEFAULT,
          D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_HEAP_FLAG_SHARED);
    } else {
      gst_d3d12_allocation_params_set_resource_flags (params,
          D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
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

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_webview2_src_event (GstBaseSrc * src, GstEvent * event)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  std::unique_lock < std::mutex > lk (priv->lock);

  if (priv->object || GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION) {
    gst_webview2_object_send_event (priv->object, event);
    return TRUE;
  }
  lk.unlock ();

  return GST_BASE_SRC_CLASS (parent_class)->event (src, event);
}

static GstFlowReturn
gst_webview2_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  GstFlowReturn ret;
  GstClock *clock;
  GstClockTime base_time;
  GstClockTime now_gst;
  GstClockTime next_capture_ts;
  guint64 next_frame_no = 0;
  GstBuffer *buffer;
  gint fps_n, fps_d;
  GstClockTime dur = GST_CLOCK_TIME_NONE;

  clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
  now_gst = gst_clock_get_time (clock);
  base_time = GST_ELEMENT_CAST (self)->base_time;
  next_capture_ts = now_gst - base_time;

  fps_n = priv->info.fps_n;
  fps_d = priv->info.fps_d;

  if (fps_n > 0 && fps_d > 0) {
    next_frame_no = gst_util_uint64_scale (next_capture_ts,
        fps_n, GST_SECOND * fps_d);

    if (next_frame_no == priv->last_frame_no) {
      GstClockID id;
      GstClockReturn clock_ret;
      std::unique_lock < std::mutex > lk (priv->lock);

      next_frame_no++;

      next_capture_ts = gst_util_uint64_scale (next_frame_no,
          fps_d * GST_SECOND, fps_n);

      id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (self),
          next_capture_ts + base_time);
      priv->clock_id = id;
      lk.unlock ();

      clock_ret = gst_clock_id_wait (id, nullptr);

      lk.lock ();

      gst_clock_id_unref (id);
      priv->clock_id = nullptr;

      if (clock_ret == GST_CLOCK_UNSCHEDULED) {
        gst_object_unref (clock);
        return GST_FLOW_FLUSHING;
      }

      dur = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    } else {
      GstClockTime next_frame_ts = gst_util_uint64_scale (next_frame_no + 1,
          fps_d * GST_SECOND, fps_n);
      dur = next_frame_ts - next_capture_ts;
    }
  }
  gst_clear_object (&clock);

  priv->last_frame_no = next_frame_no;

  auto pool = gst_base_src_get_buffer_pool (src);
  if (!pool) {
    GST_ERROR_OBJECT (self, "No configured pool");
    return GST_FLOW_ERROR;
  }
  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, nullptr);
  gst_object_unref (pool);
  if (ret != GST_FLOW_OK)
    return ret;

  auto device = gst_d3d11_device_get_device_handle (priv->device);

  ComPtr < ID3D11Texture2D > out_texture;
  gboolean system_copy = TRUE;
  gboolean is_d3d12 = FALSE;
  GstMapInfo out_map;
  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (gst_is_d3d11_memory (mem)) {
    auto dmem = GST_D3D11_MEMORY_CAST (mem);
    if (dmem->device == priv->device) {
      if (!gst_memory_map (mem, &out_map, (GstMapFlags)
              (GST_MAP_D3D11 | GST_MAP_WRITE))) {
        GST_ERROR_OBJECT (self, "Couldn't map output memory");
        gst_buffer_unref (buffer);
        return GST_FLOW_ERROR;
      }

      out_texture = (ID3D11Texture2D *) out_map.data;
      system_copy = FALSE;
    }
  } else if (priv->can_d3d12_copy && gst_is_d3d12_memory (mem)) {
    auto dmem = GST_D3D12_MEMORY_CAST (mem);
    if (gst_d3d12_device_is_equal (dmem->device, priv->device12)) {
      out_texture = gst_d3d12_memory_get_d3d11_texture (dmem, device);
      if (out_texture) {
        gst_d3d12_memory_sync (dmem);

        if (!gst_memory_map (mem, &out_map, GST_MAP_WRITE_D3D12)) {
          GST_ERROR_OBJECT (self, "Couldn't map output d3d12 memory");
          gst_buffer_unref (buffer);
          return GST_FLOW_ERROR;
        }

        is_d3d12 = TRUE;
        system_copy = FALSE;
      }
    }
  }

  if (!out_texture) {
    if (!priv->staging) {
      D3D11_TEXTURE2D_DESC staging_desc = { };
      staging_desc.Width = priv->info.width;
      staging_desc.Height = priv->info.height;
      staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      staging_desc.MipLevels = 1;
      staging_desc.SampleDesc.Count = 1;
      staging_desc.ArraySize = 1;
      staging_desc.Usage = D3D11_USAGE_STAGING;
      staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

      auto hr = device->CreateTexture2D (&staging_desc,
          nullptr, &priv->staging);
      if (!gst_d3d11_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create staging texture");
        gst_buffer_unref (buffer);
        return GST_FLOW_ERROR;
      }
    }

    out_texture = priv->staging;
    GST_TRACE_OBJECT (self, "Do CPU copy");
  } else {
    GST_TRACE_OBJECT (self, "Do GPU copy");
  }

  ret = gst_webview2_object_do_capture (priv->object, out_texture.Get (),
      priv->context4.Get (), priv->fence11.Get (), &priv->fence_val, is_d3d12);
  if (ret != GST_FLOW_OK) {
    if (!system_copy)
      gst_memory_unmap (mem, &out_map);
    gst_buffer_unref (buffer);
    return ret;
  }

  if (!system_copy) {
    gst_memory_unmap (mem, &out_map);
    if (is_d3d12) {
      auto dmem = GST_D3D12_MEMORY_CAST (mem);
      gst_d3d12_memory_set_fence (dmem, priv->fence12.Get (), priv->fence_val,
          FALSE);
    }
  } else {
    auto context = gst_d3d11_device_get_device_context_handle (priv->device);
    D3D11_MAPPED_SUBRESOURCE map;
    GstD3D11DeviceLockGuard lk (priv->device);
    auto hr = context->Map (priv->staging.Get (), 0, D3D11_MAP_READ, 0, &map);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't map staging texture");
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }

    GstVideoFrame frame;
    if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "Couldn't map output frame");
      context->Unmap (priv->staging.Get (), 0);
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }

    auto width_in_bytes = priv->info.width * 4;
    auto src_data = (guint8 *) map.pData;
    auto dst_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);
    for (guint i = 0; i < priv->info.height; i++) {
      memcpy (dst_data, src_data, width_in_bytes);
      src_data += map.RowPitch;
      dst_data += GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);
    }

    context->Unmap (priv->staging.Get (), 0);
    gst_video_frame_unmap (&frame);
  }

  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_PTS (buffer) = next_capture_ts;
  GST_BUFFER_DURATION (buffer) = dur;
  *buf = buffer;

  return GST_FLOW_OK;
}

static GstURIType
gst_webview2_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_webview2_src_get_protocols (GType type)
{
  static const gchar *protocols[] = { "web+http", "web+https", nullptr };

  return protocols;
}

static gchar *
gst_webview2_src_get_uri (GstURIHandler * handler)
{
  auto self = GST_WEBVIEW2_SRC (handler);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (priv->location.empty ())
    return nullptr;

  return g_strdup (priv->location.c_str ());
}

static gboolean
gst_webview2_src_set_uri (GstURIHandler * handler, const gchar * uri_str,
    GError ** err)
{
  auto self = GST_WEBVIEW2_SRC (handler);
  auto priv = self->priv;

  auto protocol = gst_uri_get_protocol (uri_str);
  if (!g_str_has_prefix (protocol, "web+")) {
    g_free (protocol);
    return FALSE;
  }

  auto uri = gst_uri_from_string (uri_str);
  gst_uri_set_scheme (uri, protocol + 4);

  auto location = gst_uri_to_string (uri);

  std::lock_guard < std::mutex > lk (priv->lock);
  gst_webview2_src_set_location (self, location);

  gst_uri_unref (uri);
  g_free (protocol);
  g_free (location);

  return TRUE;
}

static void
gst_webview2_src_uri_handler_init (gpointer iface, gpointer data)
{
  auto handler = (GstURIHandlerInterface *) iface;

  handler->get_type = gst_webview2_src_uri_get_type;
  handler->get_protocols = gst_webview2_src_get_protocols;
  handler->get_uri = gst_webview2_src_get_uri;
  handler->set_uri = gst_webview2_src_set_uri;
}
