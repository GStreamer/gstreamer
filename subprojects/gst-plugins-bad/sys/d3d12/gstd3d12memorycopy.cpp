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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d12memorycopy.h"
#include <gst/d3d12/gstd3d12.h>
#include <gst/d3d12/gstd3d12-private.h>
#include <gst/d3d11/gstd3d11.h>
#include <gst/d3d11/gstd3d11-private.h>
#include <gst/d3d11/gstd3d11device-private.h>
#include <directx/d3dx12.h>
#include <mutex>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_memory_copy_debug);
#define GST_CAT_DEFAULT gst_d3d12_memory_copy_debug

#define META_TAG_VIDEO meta_tag_video_quark
static GQuark meta_tag_video_quark;

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
            GST_D3D11_ALL_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY
            "," GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D11_ALL_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE (GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
            GST_D3D11_ALL_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY
            "," GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D11_ALL_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE (GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)));

enum class TransferType
{
  SYSTEM,
  D3D11_TO_12,
  D3D12_TO_11,
};

enum class MemoryType
{
  SYSTEM,
  D3D12,
  D3D11,
};

enum class DeviceSearchType
{
  ANY,
  PROPERTY,
  LUID,
};

enum
{
  PROP_0,
  PROP_ADAPTER,
};

#define DEFAULT_ADAPTER -1

struct _GstD3D12MemoryCopyPrivate
{
  ~_GstD3D12MemoryCopyPrivate ()
  {
    Reset (true);
  }

  void Reset (bool full)
  {
    if (fallback_pool12)
      gst_buffer_pool_set_active (fallback_pool12, FALSE);
    gst_clear_object (&fallback_pool12);

    fence12 = nullptr;
    fence11 = nullptr;
    fence12_external = nullptr;
    fence11_external = nullptr;
    fence12_on_11 = nullptr;
    fence11_on_11 = nullptr;
    context11_4 = nullptr;
    device11_5 = nullptr;

    in_type = MemoryType::SYSTEM;
    out_type = MemoryType::SYSTEM;
    transfer_type = TransferType::SYSTEM;
    search_type = DeviceSearchType::PROPERTY;
    fence_val = 0;

    if (full) {
      luid = 0;
      gst_clear_object (&device12);
      gst_clear_object (&device11);
      gst_clear_caps (&incaps);
      gst_clear_caps (&outcaps);
    }
  }

  GstD3D12Device *device12 = nullptr;
  GstD3D11Device *device11 = nullptr;

  ComPtr < ID3D12Fence > fence12;
  ComPtr < ID3D11Fence > fence11;

  ComPtr < ID3D12Fence > fence12_external;
  ComPtr < ID3D11Fence > fence11_external;

  ComPtr < ID3D12Fence > fence12_on_11;
  ComPtr < ID3D11Fence > fence11_on_11;

  ComPtr < ID3D11Device5 > device11_5;
  ComPtr < ID3D11DeviceContext4 > context11_4;

  GstBufferPool *fallback_pool12 = nullptr;

  GstCaps *incaps = nullptr;
  GstCaps *outcaps = nullptr;

  bool is_uploader = false;
  gint64 luid = 0;
  DeviceSearchType search_type = DeviceSearchType::PROPERTY;

  GstVideoInfo info;
  TransferType transfer_type = TransferType::SYSTEM;
  MemoryType in_type = MemoryType::SYSTEM;
  MemoryType out_type = MemoryType::SYSTEM;
  UINT64 fence_val = 0;

  gint adapter = DEFAULT_ADAPTER;

  std::recursive_mutex lock;
};

static void gst_d3d12_memory_copy_finalize (GObject * object);
static void gst_d3d12_memory_copy_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_memory_copy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_d3d12_memory_copy_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d12_memory_copy_start (GstBaseTransform * trans);
static gboolean gst_d3d12_memory_copy_stop (GstBaseTransform * trans);
static gboolean gst_d3d12_memory_copy_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_d3d12_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_d3d12_memory_copy_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static gboolean gst_d3d12_memory_copy_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d12_memory_copy_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_d3d12_memory_copy_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static void gst_d3d12_memory_copy_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static GstFlowReturn gst_d3d12_memory_copy_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

#define gst_d3d12_memory_copy_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstD3D12MemoryCopy, gst_d3d12_memory_copy,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_d3d12_memory_copy_class_init (GstD3D12MemoryCopyClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->finalize = gst_d3d12_memory_copy_finalize;
  object_class->set_property = gst_d3d12_memory_copy_set_property;
  object_class->get_property = gst_d3d12_memory_copy_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_set_context);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_stop);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_query);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_set_caps);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_transform_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_decide_allocation);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_transform_meta);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_before_transform);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d12_memory_copy_transform);

  meta_tag_video_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_STR);
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_memory_copy_debug,
      "d3d12memorycopy", 0, "d3d12memorycopy");
}

static void
gst_d3d12_memory_copy_init (GstD3D12MemoryCopy * self)
{
  self->priv = new GstD3D12MemoryCopyPrivate ();
}

static void
gst_d3d12_memory_copy_finalize (GObject * object)
{
  auto self = GST_D3D12_MEMORY_COPY (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_memory_copy_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_MEMORY_COPY (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_ADAPTER:
      priv->adapter = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_memory_copy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_MEMORY_COPY (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_memory_copy_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_D3D12_MEMORY_COPY (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    switch (priv->search_type) {
      case DeviceSearchType::ANY:
        gst_d3d12_handle_set_context (element, context, -1, &priv->device12);
        gst_d3d11_handle_set_context (element, context, -1, &priv->device11);
        break;
      case DeviceSearchType::PROPERTY:
        gst_d3d12_handle_set_context (element,
            context, priv->adapter, &priv->device12);
        gst_d3d11_handle_set_context (element,
            context, priv->adapter, &priv->device11);
        break;
      case DeviceSearchType::LUID:
        gst_d3d12_handle_set_context_for_adapter_luid (element,
            context, priv->luid, &priv->device12);
        gst_d3d11_handle_set_context_for_adapter_luid (element,
            context, priv->luid, &priv->device11);
        break;
    }
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d12_memory_copy_start (GstBaseTransform * trans)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!gst_d3d12_ensure_element_data (GST_ELEMENT_CAST (self),
          priv->adapter, &priv->device12)) {
    GST_ERROR_OBJECT (self, "Failed to get D3D12 device");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_memory_copy_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  priv->Reset (true);

  return TRUE;
}

static gboolean
gst_d3d12_memory_copy_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    auto elem = GST_ELEMENT (trans);
    if (gst_d3d12_handle_context_query (elem, query, priv->device12))
      return TRUE;

    if (gst_d3d11_handle_context_query (elem, query, priv->device11))
      return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static gboolean
gst_d3d12_memory_copy_setup_resource (GstD3D12MemoryCopy * self)
{
  auto priv = self->priv;

  priv->transfer_type = TransferType::SYSTEM;

  if (priv->in_type == priv->out_type)
    return TRUE;

  if (priv->in_type != MemoryType::D3D11 && priv->out_type != MemoryType::D3D11)
    return TRUE;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  g_object_get (priv->device12, "adapter-luid", &priv->luid, nullptr);

  if (priv->device11) {
    gint64 luid11;
    g_object_get (priv->device11, "adapter-luid", &luid11, nullptr);
    if (luid11 != priv->luid)
      gst_clear_object (&priv->device11);
  }

  if (!priv->device11) {
    priv->search_type = DeviceSearchType::LUID;
    auto elem = GST_ELEMENT (self);
    auto ret = gst_d3d11_ensure_element_data_for_adapter_luid (elem,
        priv->luid, &priv->device11);
    priv->search_type = DeviceSearchType::PROPERTY;

    if (!ret) {
      priv->search_type = DeviceSearchType::ANY;
      auto ret = gst_d3d11_ensure_element_data (elem, -1, &priv->device11);
      priv->search_type = DeviceSearchType::PROPERTY;

      if (!ret) {
        GST_ERROR_OBJECT (self, "Couldn't get any D3D11 device");
        return FALSE;
      }

      GST_WARNING_OBJECT (self, "Couldn't find matching d3d11 device");
      return TRUE;
    }
  }

  gboolean is_hardware = FALSE;
  g_object_get (priv->device11, "hardware", &is_hardware, nullptr);
  if (!is_hardware) {
    GST_INFO_OBJECT (self, "WARP device does not support resource sharing");
    return TRUE;
  }

  auto device12 = gst_d3d12_device_get_device_handle (priv->device12);
  auto format = GST_VIDEO_INFO_FORMAT (&priv->info);
  switch (format) {
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_BGR15:
      /* d3d12 will use DXGI format whereas d3d11 uses custom format */
      GST_INFO_OBJECT (self, "Different DXGI format");
      return TRUE;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    {
      /* D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_1 can support this format */
      D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4 = { };
      auto hr = device12->CheckFeatureSupport (D3D12_FEATURE_D3D12_OPTIONS4,
          &options4, sizeof (options4));
      if (FAILED (hr) || options4.SharedResourceCompatibilityTier <
          D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_1) {
        GST_INFO_OBJECT (self, "SharedResourceCompatibilityTier < "
            "D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_1");
        return TRUE;
      }
      break;
    }
    default:
      if (!gst_d3d11_device_d3d12_import_supported (priv->device11)) {
        GST_INFO_OBJECT (self, "Cross-api resource sharing is not supported");
        return TRUE;
      }
      break;
  }

  GstD3D12Format format12 = { };
  gst_d3d12_device_get_format (priv->device12, format, &format12);
  if ((format12.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) !=
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
    GST_INFO_OBJECT (self,
        "Format does not support render target, disable interop");
    return TRUE;
  }

  GstD3D11Format format11 = { };
  gst_d3d11_device_get_format (priv->device11, format, &format11);
  if (format12.dxgi_format != format11.dxgi_format) {
    GST_INFO_OBJECT (self, "Different DXGI formats are used, need system copy");
    return TRUE;
  }

  auto device11 = gst_d3d11_device_get_device_handle (priv->device11);
  ComPtr < ID3D11Device5 > device11_5;
  auto hr = device11->QueryInterface (IID_PPV_ARGS (&device11_5));
  if (FAILED (hr)) {
    GST_INFO_OBJECT (self, "Device does not support ID3D11Device5 interface");
    return TRUE;
  }

  ComPtr < ID3D11DeviceContext4 > context11_4;
  auto context11 = gst_d3d11_device_get_device_context_handle (priv->device11);
  hr = context11->QueryInterface (IID_PPV_ARGS (&context11_4));
  if (FAILED (hr)) {
    GST_INFO_OBJECT (self,
        "Device does not support ID3D11DeviceContext4 interface");
    return TRUE;
  }

  if (priv->in_type == MemoryType::D3D12) {
    priv->fallback_pool12 = gst_d3d12_buffer_pool_new (priv->device12);
    auto config = gst_buffer_pool_get_config (priv->fallback_pool12);
    /* Default buffer pool allocates resource in shared heap */
    auto caps = gst_video_info_to_caps (&priv->info);
    gst_buffer_pool_config_set_params (config, caps, priv->info.size, 0, 0);
    gst_caps_unref (caps);
    if (!gst_buffer_pool_set_config (priv->fallback_pool12, config)) {
      GST_WARNING_OBJECT (self, "Couldn't set pool config");
      gst_clear_object (&priv->fallback_pool12);
      return TRUE;
    }

    if (!gst_buffer_pool_set_active (priv->fallback_pool12, TRUE)) {
      GST_WARNING_OBJECT (self, "Couldn't active fallback pool");
      gst_clear_object (&priv->fallback_pool12);
      return TRUE;
    }
  }

  /* Creates fence on 11 in order to signal to d3d12 when d3d11 is no longer
   * using the shared handle */
  hr = device11_5->CreateFence (0, D3D11_FENCE_FLAG_SHARED,
      IID_PPV_ARGS (&priv->fence11_on_11));
  if (!gst_d3d11_result (hr, priv->device11)) {
    GST_WARNING_OBJECT (self, "Couldn't create d3d11 fence");
    return TRUE;
  }

  HANDLE handle;
  hr = priv->fence11_on_11->CreateSharedHandle (nullptr, GENERIC_ALL, nullptr,
      &handle);
  if (!gst_d3d11_result (hr, priv->device11)) {
    GST_WARNING_OBJECT (self, "Couldn't create shared fence handle");
    return TRUE;
  }

  hr = device12->OpenSharedHandle (handle, IID_PPV_ARGS (&priv->fence12_on_11));
  CloseHandle (handle);
  if (!gst_d3d12_result (hr, priv->device12)) {
    GST_WARNING_OBJECT (self, "Couldn't open shared fence");
    return TRUE;
  }

  if (priv->in_type == MemoryType::D3D11)
    priv->transfer_type = TransferType::D3D11_TO_12;
  else
    priv->transfer_type = TransferType::D3D12_TO_11;

  if (priv->transfer_type == TransferType::D3D12_TO_11) {
    priv->fence12 = gst_d3d12_device_get_fence_handle (priv->device12,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    hr = device12->CreateSharedHandle (priv->fence12.Get (), nullptr,
        GENERIC_ALL, nullptr, &handle);
    if (!gst_d3d12_result (hr, priv->device12)) {
      GST_WARNING_OBJECT (self, "Couldn't create shared fence handle");
      priv->transfer_type = TransferType::SYSTEM;
      return TRUE;
    }

    hr = device11_5->OpenSharedFence (handle, IID_PPV_ARGS (&priv->fence11));
    CloseHandle (handle);
    if (!gst_d3d11_result (hr, priv->device11)) {
      GST_WARNING_OBJECT (self, "Couldn't open shared fence");
      priv->transfer_type = TransferType::SYSTEM;
      return TRUE;
    }
  }

  priv->device11_5 = device11_5;
  priv->context11_4 = context11_4;

  return TRUE;
}

static gboolean
gst_d3d12_memory_copy_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;

  if (!priv->device12) {
    GST_ERROR_OBJECT (self, "No available D3D12 device");
    return FALSE;
  }

  gst_caps_replace (&priv->incaps, incaps);
  gst_caps_replace (&priv->outcaps, outcaps);

  if (!gst_video_info_from_caps (&priv->info, incaps)) {
    GST_ERROR_OBJECT (self, "Invalid input caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  priv->Reset (false);

  auto features = gst_caps_get_features (incaps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
    priv->in_type = MemoryType::D3D12;
  } else if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    priv->in_type = MemoryType::D3D11;
  }

  features = gst_caps_get_features (outcaps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
    priv->out_type = MemoryType::D3D12;
  } else if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    priv->out_type = MemoryType::D3D11;
  }

  return gst_d3d12_memory_copy_setup_resource (self);
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++) {
    gst_caps_set_features (tmp, i,
        gst_caps_features_from_string (feature_name));
  }

  return tmp;
}

static GstCaps *
gst_d3d12_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    if (priv->is_uploader) {
      auto caps_12 =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);
      tmp = gst_caps_merge (caps_12, gst_caps_ref (caps));
    } else {
      auto caps_11 =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);
      auto caps_sys =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
      tmp = gst_caps_merge (caps_11, caps_sys);
      tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
    }
  } else {
    if (priv->is_uploader) {
      auto caps_11 =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);
      auto caps_sys =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);

      tmp = gst_caps_merge (caps_11, caps_sys);
      tmp = gst_caps_merge (tmp, gst_caps_ref (caps));
    } else {
      auto caps_12 =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);
      tmp = gst_caps_merge (caps_12, gst_caps_ref (caps));
    }
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_d3d12_memory_copy_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;
  bool is_d3d12 = false;
  bool is_d3d11 = false;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (!decide_query)
    return TRUE;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_WARNING_OBJECT (self, "Allocation query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) == 0) {
    auto features = gst_caps_get_features (caps, 0);
    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
      GST_DEBUG_OBJECT (self, "upstream support d3d12 memory");
      pool = gst_d3d12_buffer_pool_new (priv->device12);
      is_d3d12 = true;
    } else if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
      if (!priv->device11) {
        GST_ERROR_OBJECT (self, "D3D11 device is not configured");
        return FALSE;
      }
      pool = gst_d3d11_buffer_pool_new (priv->device11);
      is_d3d11 = true;
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    auto config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (is_d3d12) {
      GstD3D12Format format12;
      gst_d3d12_device_get_format (priv->device12,
          GST_VIDEO_INFO_FORMAT (&info), &format12);

      D3D12_RESOURCE_FLAGS resource_flags =
          D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
      if ((format12.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ==
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
        resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      }

      if ((format12.format_flags & GST_D3D12_FORMAT_FLAG_OUTPUT_UAV) ==
          GST_D3D12_FORMAT_FLAG_OUTPUT_UAV &&
          (format12.support1 &
              D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) ==
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) {
        resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      }

      D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
      if (priv->transfer_type == TransferType::D3D12_TO_11)
        heap_flags = D3D12_HEAP_FLAG_SHARED;

      auto params = gst_d3d12_allocation_params_new (priv->device12,
          &info, GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
          D3D12_HEAP_FLAG_SHARED);
      gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
      gst_d3d12_allocation_params_free (params);
    } else if (is_d3d11) {
      GstD3D11Format format11;
      gst_d3d11_device_get_format (priv->device11,
          GST_VIDEO_INFO_FORMAT (&info), &format11);
      DXGI_FORMAT dxgi_format = format11.dxgi_format;
      if (dxgi_format == DXGI_FORMAT_UNKNOWN)
        dxgi_format = format11.resource_format[0];

      auto device11 = gst_d3d11_device_get_device_handle (priv->device11);
      UINT support_flags = 0;
      guint bind_flags = 0;
      device11->CheckFormatSupport (dxgi_format, &support_flags);
      if ((support_flags & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ==
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) {
        bind_flags |= D3D11_BIND_SHADER_RESOURCE;
      }

      if ((support_flags & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ==
          D3D11_FORMAT_SUPPORT_RENDER_TARGET) {
        bind_flags |= D3D11_BIND_RENDER_TARGET;
      }

      auto params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
      if (!params) {
        params = gst_d3d11_allocation_params_new (priv->device11, &info,
            GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, 0);
      }
      gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
      gst_d3d11_allocation_params_free (params);
    } else {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    }

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (self, "Bufferpool config failed");
      gst_object_unref (pool);
      return FALSE;
    }

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config,
        nullptr, &size, nullptr, nullptr);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_d3d12_memory_copy_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;
  GstBufferPool *pool = nullptr;
  GstVideoInfo info;
  guint min, max, size;
  GstCaps *caps = nullptr;
  bool update_pool = false;
  bool is_d3d12 = false;
  bool is_d3d11 = false;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_WARNING_OBJECT (self, "Allocation query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = true;
  } else {
    size = info.size;
    min = max = 0;
  }

  auto features = gst_caps_get_features (caps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
    GST_DEBUG_OBJECT (self, "upstream support d3d12 memory");
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, priv->device12))
          gst_clear_object (&pool);
      }
    }

    if (!pool)
      pool = gst_d3d12_buffer_pool_new (priv->device12);

    is_d3d12 = true;
  } else if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    if (!priv->device11) {
      GST_ERROR_OBJECT (self, "D3D11 device is not configured");
      return FALSE;
    }

    if (pool) {
      if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D11_BUFFER_POOL (pool);
        if (dpool->device != priv->device11)
          gst_clear_object (&pool);
      }
    }

    if (!pool)
      pool = gst_d3d11_buffer_pool_new (priv->device11);

    is_d3d11 = true;
  } else if (!pool) {
    pool = gst_video_buffer_pool_new ();
  }

  if (!pool) {
    pool = gst_video_buffer_pool_new ();
  }

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (is_d3d12) {
    GstD3D12Format format12;
    gst_d3d12_device_get_format (priv->device12, GST_VIDEO_INFO_FORMAT (&info),
        &format12);

    D3D12_RESOURCE_FLAGS resource_flags =
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    if ((format12.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ==
        D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
      resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    auto params = gst_buffer_pool_config_get_d3d12_allocation_params (config);
    if (params) {
      gst_d3d12_allocation_params_set_resource_flags (params, resource_flags);
      if (priv->transfer_type != TransferType::SYSTEM) {
        gst_d3d12_allocation_params_set_heap_flags (params,
            D3D12_HEAP_FLAG_SHARED);
      }
    } else {
      params = gst_d3d12_allocation_params_new (priv->device12, &info,
          GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
          D3D12_HEAP_FLAG_SHARED);
    }

    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);
  } else if (is_d3d11) {
    GstD3D11Format format11;
    gst_d3d11_device_get_format (priv->device11, GST_VIDEO_INFO_FORMAT (&info),
        &format11);
    DXGI_FORMAT dxgi_format = format11.dxgi_format;
    if (dxgi_format == DXGI_FORMAT_UNKNOWN)
      dxgi_format = format11.resource_format[0];

    auto device11 = gst_d3d11_device_get_device_handle (priv->device11);
    UINT support_flags = 0;
    guint bind_flags = 0;
    device11->CheckFormatSupport (dxgi_format, &support_flags);
    if ((support_flags & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ==
        D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) {
      bind_flags |= D3D11_BIND_SHADER_RESOURCE;
    }

    if ((support_flags & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ==
        D3D11_FORMAT_SUPPORT_RENDER_TARGET) {
      bind_flags |= D3D11_BIND_RENDER_TARGET;
    }

    auto params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!params) {
      params = gst_d3d11_allocation_params_new (priv->device11, &info,
          GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, 0);
    }
    gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
    gst_d3d11_allocation_params_free (params);
  }

  gst_buffer_pool_set_config (pool, config);

  /* d3d12/d3d11 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_d3d12_memory_copy_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  const GstMetaInfo *info = meta->info;
  const gchar *const *tags;

  tags = gst_meta_api_type_get_tags (info->api);

  if (!tags || (g_strv_length ((gchar **) tags) == 1
          && gst_meta_api_type_has_tag (info->api, META_TAG_VIDEO))) {
    return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans, outbuf,
      meta, inbuf);
}

static void
gst_d3d12_memory_copy_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;
  bool need_reconfigure = false;

  if (priv->transfer_type == TransferType::SYSTEM)
    return;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (priv->in_type == MemoryType::D3D11) {
    if (!gst_is_d3d11_memory (mem)) {
      GST_WARNING_OBJECT (self, "Input memory is not d3d11");
      priv->transfer_type = TransferType::SYSTEM;
      return;
    }

    auto dmem = GST_D3D11_MEMORY_CAST (mem);
    if (dmem->device != priv->device11) {
      GST_INFO_OBJECT (self, "d3d11 device is updated");
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      gst_clear_object (&priv->device11);
      priv->device11 = (GstD3D11Device *) gst_object_ref (dmem->device);

      gint64 luid11;
      gint64 luid12;
      g_object_get (priv->device11, "adapter-luid", &luid11, nullptr);
      g_object_get (priv->device12, "adapter-luid", &luid12, nullptr);

      if (luid11 != luid12) {
        auto prev_device12 = priv->device12;
        priv->device12 = nullptr;
        priv->search_type = DeviceSearchType::LUID;
        priv->luid = luid11;
        if (!gst_d3d12_ensure_element_data_for_adapter_luid (GST_ELEMENT (self),
                luid11, &priv->device12)) {
          GST_WARNING_OBJECT (self, "Couldn't get d3d12 device");
          priv->search_type = DeviceSearchType::PROPERTY;
          priv->transfer_type = TransferType::SYSTEM;
          priv->device12 = prev_device12;
          return;
        }
        priv->search_type = DeviceSearchType::PROPERTY;

        gst_clear_object (&prev_device12);
        need_reconfigure = true;
      }
    }
  } else if (priv->in_type == MemoryType::D3D12) {
    if (!gst_is_d3d12_memory (mem)) {
      GST_WARNING_OBJECT (self, "Input memory is not d3d12");
      priv->transfer_type = TransferType::SYSTEM;
      return;
    }

    auto dmem = GST_D3D12_MEMORY_CAST (mem);
    if (!gst_d3d12_device_is_equal (dmem->device, priv->device12)) {
      GST_INFO_OBJECT (self, "d3d12 device is updated");
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      gst_clear_object (&priv->device12);
      priv->device12 = (GstD3D12Device *) gst_object_ref (dmem->device);

      g_object_get (priv->device12, "adapter-luid", &priv->luid, nullptr);

      auto prev_device11 = priv->device11;
      priv->device11 = nullptr;
      priv->search_type = DeviceSearchType::LUID;
      if (!gst_d3d11_ensure_element_data_for_adapter_luid (GST_ELEMENT (self),
              priv->luid, &priv->device11)) {
        GST_WARNING_OBJECT (self, "Couldn't get d3d11 device");
        priv->search_type = DeviceSearchType::PROPERTY;
        priv->transfer_type = TransferType::SYSTEM;
        priv->device11 = prev_device11;
        return;
      }
      priv->search_type = DeviceSearchType::PROPERTY;

      gst_clear_object (&prev_device11);
      need_reconfigure = true;
    }
  }

  if (need_reconfigure) {
    GST_DEBUG_OBJECT (self, "Reconfiguring for device update");
    gst_d3d12_memory_copy_set_caps (trans, priv->incaps, priv->outcaps);
    gst_base_transform_reconfigure_src (trans);
  }
}

static GstFlowReturn
gst_d3d12_memory_copy_system_copy (GstD3D12MemoryCopy * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  auto priv = self->priv;
  GstVideoFrame in_frame, out_frame;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!gst_video_frame_map (&in_frame, &priv->info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input frame");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&out_frame, &priv->info, outbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    gst_video_frame_unmap (&in_frame);
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_copy (&out_frame, &in_frame)) {
    GST_ERROR_OBJECT (self, "Copy failed");
    ret = GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;
}

static gboolean
gst_d3d12_memory_copy_11_to_12 (GstD3D12MemoryCopy * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  auto priv = self->priv;

  GstD3D11DeviceLockGuard lk (priv->device11);
  for (guint i = 0; i < gst_buffer_n_memory (inbuf); i++) {
    GstMapInfo in_map;
    auto in_mem = gst_buffer_peek_memory (inbuf, i);
    auto in_mem11 = GST_D3D11_MEMORY_CAST (in_mem);
    auto out_mem12 = (GstD3D12Memory *) gst_buffer_peek_memory (outbuf, i);

    auto out_tex11 = gst_d3d12_memory_get_d3d11_texture (out_mem12,
        priv->device11_5.Get ());
    if (!out_tex11) {
      GST_ERROR_OBJECT (self, "Couldn't get shared texture11");
      return FALSE;
    }

    if (!gst_memory_map (in_mem, &in_map,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Couldn't map input memory");
      return FALSE;
    }

    auto in_tex11 = (ID3D11Texture2D *) in_map.data;
    D3D11_TEXTURE2D_DESC in_desc;
    D3D11_TEXTURE2D_DESC out_desc;

    in_tex11->GetDesc (&in_desc);
    out_tex11->GetDesc (&out_desc);

    if (in_desc.Format != out_desc.Format) {
      GST_ERROR_OBJECT (self, "Different DXGI format");
      gst_memory_unmap (in_mem, &in_map);
      return FALSE;
    }

    auto subresource = gst_d3d11_memory_get_subresource_index (in_mem11);

    D3D11_BOX src_box = { };
    src_box.back = 1;
    src_box.right = MIN (in_desc.Width, out_desc.Width);
    src_box.bottom = MIN (in_desc.Height, out_desc.Height);

    priv->context11_4->CopySubresourceRegion (out_tex11, 0, 0, 0, 0,
        in_tex11, subresource, &src_box);
    gst_memory_unmap (in_mem, &in_map);
  }

  priv->fence_val++;
  auto hr = priv->context11_4->Signal (priv->fence11_on_11.Get (),
      priv->fence_val);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Signal failed");
    return FALSE;
  }

  gst_d3d12_buffer_set_fence (outbuf,
      priv->fence12_on_11.Get (), priv->fence_val, FALSE);

  return TRUE;
}

static gboolean
gst_d3d12_memory_copy_12_to_11 (GstD3D12MemoryCopy * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  auto priv = self->priv;

  for (guint i = 0; i < gst_buffer_n_memory (inbuf); i++) {
    GstMapInfo in_map, out_map;
    auto in_mem = gst_buffer_peek_memory (inbuf, i);
    auto in_mem12 = GST_D3D12_MEMORY_CAST (in_mem);
    auto out_mem = gst_buffer_peek_memory (outbuf, i);

    if (!gst_memory_map (in_mem, &in_map, GST_MAP_READ_D3D12)) {
      GST_ERROR_OBJECT (self, "Couldn't map input memory");
      return FALSE;
    }

    if (!gst_memory_map (out_mem, &out_map,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Couldn't map output memory");
      gst_memory_unmap (in_mem, &in_map);
      return FALSE;
    }

    auto in_tex11 = gst_d3d12_memory_get_d3d11_texture (in_mem12,
        priv->device11_5.Get ());
    if (!in_tex11) {
      GST_ERROR_OBJECT (self, "Couldn't get shared texture11");
      gst_memory_unmap (out_mem, &out_map);
      gst_memory_unmap (in_mem, &in_map);
      return FALSE;
    }

    auto out_tex11 = (ID3D11Texture2D *) out_map.data;
    D3D11_TEXTURE2D_DESC in_desc;
    D3D11_TEXTURE2D_DESC out_desc;

    in_tex11->GetDesc (&in_desc);
    out_tex11->GetDesc (&out_desc);

    if (in_desc.Format != out_desc.Format) {
      GST_ERROR_OBJECT (self, "Different DXGI format");
      gst_memory_unmap (out_mem, &out_map);
      gst_memory_unmap (in_mem, &in_map);
      return FALSE;
    }

    ComPtr < ID3D12Fence > fence12;
    guint64 fence_val = 0;
    HRESULT hr;
    if (gst_d3d12_memory_get_fence (in_mem12, &fence12, &fence_val)) {
      auto completed = fence12->GetCompletedValue ();
      if (completed < fence_val) {
        GST_TRACE_OBJECT (self, "Completed %" G_GUINT64_FORMAT
            " < WaitValue %" G_GUINT64_FORMAT, completed, fence_val);

        if (fence12.Get () == priv->fence12.Get ()) {
          GstD3D11DeviceLockGuard lk (priv->device11);
          hr = priv->context11_4->Wait (priv->fence11.Get (), fence_val);
          if (FAILED (hr)) {
            GST_ERROR_OBJECT (self, "Wait failed");
            gst_memory_unmap (out_mem, &out_map);
            gst_memory_unmap (in_mem, &in_map);
            return FALSE;
          }
        } else if (priv->fence12_external == fence12) {
          GST_LOG_OBJECT (self, "Reuse shared fence");
          GstD3D11DeviceLockGuard lk (priv->device11);
          hr = priv->context11_4->Wait (priv->fence11_external.Get (),
              fence_val);
          if (FAILED (hr)) {
            GST_ERROR_OBJECT (self, "Wait failed");
            gst_memory_unmap (out_mem, &out_map);
            gst_memory_unmap (in_mem, &in_map);
            return FALSE;
          }
        } else {
          ComPtr < ID3D12Fence1 > fence12_1;
          bool need_sync = true;

          hr = fence12.As (&fence12_1);
          if (SUCCEEDED (hr)) {
            if ((fence12_1->GetCreationFlags () & D3D12_FENCE_FLAG_SHARED) ==
                D3D12_FENCE_FLAG_SHARED) {
              auto device12 =
                  gst_d3d12_device_get_device_handle (priv->device12);
              HANDLE handle;
              hr = device12->CreateSharedHandle (fence12.Get (), nullptr,
                  GENERIC_ALL, nullptr, &handle);
              if (SUCCEEDED (hr)) {
                ComPtr < ID3D11Fence > fence11;
                hr = priv->device11_5->OpenSharedFence (handle,
                    IID_PPV_ARGS (&fence11));
                CloseHandle (handle);

                if (SUCCEEDED (hr)) {
                  GST_DEBUG_OBJECT (self, "Opened new external shared fence");
                  priv->fence12_external = fence12;
                  priv->fence11_external = fence11;

                  GstD3D11DeviceLockGuard lk (priv->device11);
                  hr = priv->context11_4->Wait (fence11.Get (), fence_val);
                  if (FAILED (hr)) {
                    GST_ERROR_OBJECT (self, "Wait failed");
                    gst_memory_unmap (out_mem, &out_map);
                    gst_memory_unmap (in_mem, &in_map);
                    return FALSE;
                  }

                  need_sync = false;
                }
              }
            }
          }

          if (need_sync)
            gst_d3d12_memory_sync (in_mem12);
        }
      }
    }

    D3D11_BOX src_box = { };
    src_box.back = 1;
    src_box.right = MIN (in_desc.Width, out_desc.Width);
    src_box.bottom = MIN (in_desc.Height, out_desc.Height);

    {
      GstD3D11DeviceLockGuard lk (priv->device11);
      priv->context11_4->CopySubresourceRegion (out_tex11, 0, 0, 0, 0,
          in_tex11, 0, &src_box);

      priv->fence_val++;
      hr = priv->context11_4->Signal (priv->fence11_on_11.Get (),
          priv->fence_val);
    }

    gst_memory_unmap (out_mem, &out_map);
    gst_memory_unmap (in_mem, &in_map);

    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Signal failed");
      return FALSE;
    }

    gst_d3d12_memory_set_fence (in_mem12,
        priv->fence12_on_11.Get (), priv->fence_val, FALSE);
  }

  return TRUE;
}

static GstBuffer *
gst_d3d12_memory_copy_upload (GstD3D12MemoryCopy * self, GstBuffer * buffer)
{
  auto priv = self->priv;

  if (priv->transfer_type == TransferType::D3D12_TO_11) {
    bool need_copy = false;
    for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
      auto dmem12 = (GstD3D12Memory *) gst_buffer_peek_memory (buffer, i);
      auto tex11 = gst_d3d12_memory_get_d3d11_texture (dmem12,
          priv->device11_5.Get ());
      if (!tex11) {
        need_copy = true;
        break;
      }
    }

    if (need_copy) {
      GstBuffer *upload_buf = nullptr;

      gst_buffer_pool_acquire_buffer (priv->fallback_pool12,
          &upload_buf, nullptr);
      if (!upload_buf) {
        GST_ERROR_OBJECT (self, "Couldn't acquire upload buffer");
        priv->transfer_type = TransferType::SYSTEM;
        return gst_buffer_ref (buffer);
      }

      if (!gst_d3d12_buffer_copy_into (upload_buf, buffer, &priv->info)) {
        GST_ERROR_OBJECT (self, "Couldn't copy to intermediate buffer");
        gst_buffer_unref (upload_buf);
        priv->transfer_type = TransferType::SYSTEM;
        return gst_buffer_ref (buffer);
      }

      return upload_buf;
    }
  }

  return gst_buffer_ref (buffer);
}

static GstFlowReturn
gst_d3d12_memory_copy_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_D3D12_MEMORY_COPY (trans);
  auto priv = self->priv;

  if (priv->transfer_type != TransferType::SYSTEM) {
    if (gst_buffer_n_memory (inbuf) != gst_buffer_n_memory (outbuf)) {
      GST_WARNING_OBJECT (self, "Different memory layout");
      priv->transfer_type = TransferType::SYSTEM;
    }
  }

  GstBuffer *upload_buf = gst_d3d12_memory_copy_upload (self, inbuf);
  if (!upload_buf) {
    GST_ERROR_OBJECT (self, "Null upload buffer");
    return GST_FLOW_ERROR;
  }

  if (priv->transfer_type == TransferType::D3D11_TO_12) {
    if (gst_d3d12_memory_copy_11_to_12 (self, upload_buf, outbuf)) {
      GST_LOG_OBJECT (self, "Copy 11-to-12 done");
      gst_buffer_unref (upload_buf);
      return GST_FLOW_OK;
    }

    priv->transfer_type = TransferType::SYSTEM;
  } else if (priv->transfer_type == TransferType::D3D12_TO_11) {
    if (gst_d3d12_memory_copy_12_to_11 (self, upload_buf, outbuf)) {
      GST_LOG_OBJECT (self, "Copy 12-to-11 done");
      gst_buffer_unref (upload_buf);
      return GST_FLOW_OK;
    }

    priv->transfer_type = TransferType::SYSTEM;
  }

  auto ret = gst_d3d12_memory_copy_system_copy (self, upload_buf, outbuf);
  gst_buffer_unref (upload_buf);

  return ret;
}

struct _GstD3D12Upload
{
  GstD3D12MemoryCopy parent;
};

G_DEFINE_TYPE (GstD3D12Upload, gst_d3d12_upload, GST_TYPE_D3D12_MEMORY_COPY);

static void
gst_d3d12_upload_class_init (GstD3D12UploadClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Uploader", "Filter/Video",
      "Uploads system memory into Direct3D12 texture memory",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_d3d12_upload_init (GstD3D12Upload * self)
{
  auto memcpy = GST_D3D12_MEMORY_COPY (self);
  memcpy->priv->is_uploader = true;
}

struct _GstD3D12Download
{
  GstD3D12MemoryCopy parent;
};

G_DEFINE_TYPE (GstD3D12Download, gst_d3d12_download,
    GST_TYPE_D3D12_MEMORY_COPY);

static void
gst_d3d12_download_class_init (GstD3D12DownloadClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Downloader", "Filter/Video",
      "Downloads Direct3D12 texture memory into system memory",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_d3d12_download_init (GstD3D12Download * self)
{
  auto memcpy = GST_D3D12_MEMORY_COPY (self);
  memcpy->priv->is_uploader = false;
}
