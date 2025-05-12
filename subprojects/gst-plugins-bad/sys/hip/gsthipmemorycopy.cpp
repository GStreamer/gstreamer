/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gsthip-config.h"

#ifdef HAVE_GST_CUDA
#include <gst/cuda/gstcuda.h>
#else
#define GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY "memory:CUDAMemory"
#endif

#include "gsthip.h"
#include "gsthipmemorycopy.h"
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_hip_memory_copy_debug);
#define GST_CAT_DEFAULT gst_hip_memory_copy_debug

#define GST_HIP_FORMATS \
    "{ I420, YV12, NV12, NV21, P010_10LE, P012_LE, P016_LE, I420_10LE, I420_12LE, Y444, " \
    "Y444_10LE, Y444_12LE, Y444_16LE, BGRA, RGBA, RGBx, BGRx, ARGB, ABGR, RGB, " \
    "BGR, BGR10A2_LE, RGB10A2_LE, Y42B, I422_10LE, I422_12LE, YUY2, UYVY, RGBP, " \
    "BGRP, GBR, GBR_10LE, GBR_12LE, GBR_16LE, GBRA, VUYA }"

#ifdef HAVE_GST_CUDA
#define CAPS_STR \
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY "," \
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, \
        GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES  \
    (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY "," \
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, \
        GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE (GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY "," \
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, \
        GST_HIP_FORMATS)
#else
#define CAPS_STR \
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY "," \
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, \
        GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE (GST_HIP_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY "," \
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, \
        GST_HIP_FORMATS)
#endif

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR));

enum class TransferType
{
  SYSTEM,
  CUDA_TO_HIP,
  HIP_TO_CUDA,
};

enum class MemoryType
{
  SYSTEM,
  HIP,
  CUDA,
};

enum class DeviceSearchType
{
  ANY,
  PROPERTY,
  DEVICE_ID,
};

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_VENDOR,
};

#define DEFAULT_DEVICE_ID -1
#define DEFAULT_VENDOR GST_HIP_VENDOR_UNKNOWN

/* *INDENT-OFF* */
struct _GstHipMemoryCopyPrivate
{
  ~_GstHipMemoryCopyPrivate ()
  {
    Reset (true);
  }

  void Reset (bool full)
  {
    in_type = MemoryType::SYSTEM;
    out_type = MemoryType::SYSTEM;
    transfer_type = TransferType::SYSTEM;
    search_type = DeviceSearchType::PROPERTY;

    if (full) {
      target_id = -1;
      target_vendor = GST_HIP_VENDOR_UNKNOWN;
      gst_clear_caps (&in_caps);
      gst_clear_caps (&out_caps);
      gst_clear_object (&device);
#ifdef HAVE_GST_CUDA
      gst_clear_object (&cuda_ctx);
#endif
    }
  }

  gboolean is_uploader;
  std::recursive_mutex lock;

  GstVideoInfo info;

  GstCaps *in_caps = nullptr;
  GstCaps *out_caps = nullptr;

  GstHipDevice *device = nullptr;
#ifdef HAVE_GST_CUDA
  GstCudaContext *cuda_ctx = nullptr;
#endif

  DeviceSearchType search_type = DeviceSearchType::PROPERTY;
  TransferType transfer_type = TransferType::SYSTEM;
  MemoryType in_type = MemoryType::SYSTEM;
  MemoryType out_type = MemoryType::SYSTEM;

  gint target_id = -1;
  GstHipVendor target_vendor = GST_HIP_VENDOR_UNKNOWN;

  gint device_id = DEFAULT_DEVICE_ID;
  GstHipVendor vendor = DEFAULT_VENDOR;
};
/* *INDENT-ON* */

static void gst_hip_memory_copy_finalize (GObject * object);
static void gst_hip_memory_copy_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_hip_memory_copy_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_hip_memory_copy_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_hip_memory_copy_start (GstBaseTransform * trans);
static gboolean gst_hip_memory_copy_stop (GstBaseTransform * trans);
static gboolean gst_hip_memory_copy_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_hip_memory_copy_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static void gst_hip_memory_copy_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static GstCaps *gst_hip_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_hip_memory_copy_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_hip_memory_copy_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static GstFlowReturn gst_hip_memory_copy_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

#define gst_hip_memory_copy_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstHipMemoryCopy, gst_hip_memory_copy,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_hip_memory_copy_class_init (GstHipMemoryCopyClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->finalize = gst_hip_memory_copy_finalize;
  object_class->set_property = gst_hip_memory_copy_set_property;
  object_class->get_property = gst_hip_memory_copy_get_property;

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_int ("device-id",
          "Device ID", "HIP device ID to use (-1 = auto)",
          -1, G_MAXINT, DEFAULT_DEVICE_ID,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_VENDOR,
      g_param_spec_enum ("vendor", "Vendor", "Vendor type",
          GST_TYPE_HIP_VENDOR, GST_HIP_VENDOR_UNKNOWN,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_set_context);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_hip_memory_copy_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_hip_memory_copy_stop);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_hip_memory_copy_set_caps);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_transform_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_decide_allocation);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_hip_memory_copy_query);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_before_transform);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_hip_memory_copy_transform);

  gst_type_mark_as_plugin_api (GST_TYPE_HIP_MEMORY_COPY, (GstPluginAPIFlags) 0);
  GST_DEBUG_CATEGORY_INIT (gst_hip_memory_copy_debug,
      "hipmemorycopy", 0, "hipmemorycopy");
}

static void
gst_hip_memory_copy_init (GstHipMemoryCopy * self)
{
  self->priv = new GstHipMemoryCopyPrivate ();
}

static void
gst_hip_memory_copy_finalize (GObject * object)
{
  auto self = GST_HIP_MEMORY_COPY (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_hip_memory_copy_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_MEMORY_COPY (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int (value);
      break;
    case PROP_VENDOR:
      priv->vendor = (GstHipVendor) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_memory_copy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_MEMORY_COPY (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int (value, priv->device_id);
      break;
    case PROP_VENDOR:
      g_value_set_enum (value, priv->vendor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_memory_copy_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_HIP_MEMORY_COPY (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    switch (priv->search_type) {
      case DeviceSearchType::ANY:
        gst_hip_handle_set_context (element, context, GST_HIP_VENDOR_UNKNOWN,
            -1, &priv->device);
#ifdef HAVE_GST_CUDA
        gst_cuda_handle_set_context (element, context, -1, &priv->cuda_ctx);
#endif
        break;
      case DeviceSearchType::PROPERTY:
        gst_hip_handle_set_context (element, context, priv->vendor,
            priv->device_id, &priv->device);
#ifdef HAVE_GST_CUDA
        if (priv->vendor != GST_HIP_VENDOR_AMD) {
          gst_cuda_handle_set_context (element, context, priv->device_id,
              &priv->cuda_ctx);
        }
#endif
        break;
      case DeviceSearchType::DEVICE_ID:
        gst_hip_handle_set_context (element, context, priv->target_vendor,
            priv->target_id, &priv->device);
#ifdef HAVE_GST_CUDA
        if (priv->vendor != GST_HIP_VENDOR_AMD) {
          gst_cuda_handle_set_context (element, context, priv->device_id,
              &priv->cuda_ctx);
        }
#endif
        break;
    }
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_hip_memory_copy_start (GstBaseTransform * trans)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!gst_hip_ensure_element_data (GST_ELEMENT (trans),
            priv->vendor, priv->device_id, &priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't get HIP device");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_hip_memory_copy_stop (GstBaseTransform * trans)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  priv->Reset (true);

  return TRUE;
}

#ifdef HAVE_GST_CUDA
static gboolean
gst_hip_memory_copy_ensure_device (GstHipMemoryCopy * self)
{
  auto priv = self->priv;

  if (priv->in_type == priv->out_type)
    return TRUE;

  if (priv->in_type != MemoryType::CUDA && priv->out_type != MemoryType::CUDA)
    return TRUE;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  auto elem = GST_ELEMENT (self);
  auto vendor = gst_hip_device_get_vendor (priv->device);
  if (vendor != GST_HIP_VENDOR_NVIDIA) {
    /* Create new device for NVIDIA */
    auto old_dev = priv->device;
    priv->device = nullptr;
    priv->target_id = -1;
    priv->target_vendor = GST_HIP_VENDOR_NVIDIA;
    priv->search_type = DeviceSearchType::DEVICE_ID;
    auto ret = gst_hip_ensure_element_data (elem,
        priv->target_vendor, priv->target_id, &priv->device);
    priv->search_type = DeviceSearchType::PROPERTY;
    if (!ret) {
      GST_WARNING_OBJECT (self, "Couldn't create device for NVIDIA");
      priv->device = old_dev;
      return TRUE;
    }

    gst_object_unref (old_dev);
  }

  auto device_id = gst_hip_device_get_device_id (priv->device);
  if (priv->cuda_ctx) {
    guint cuda_dev_id;
    g_object_get (priv->cuda_ctx, "cuda-device-id", &cuda_dev_id, nullptr);
    if (cuda_dev_id != device_id)
      gst_clear_object (&priv->cuda_ctx);
  }

  if (!priv->cuda_ctx) {
    priv->search_type = DeviceSearchType::DEVICE_ID;
    auto ret = gst_cuda_ensure_element_context (elem,
        device_id, &priv->cuda_ctx);
    priv->search_type = DeviceSearchType::PROPERTY;
    if (!ret) {
      GST_WARNING_OBJECT (self, "Couldn't create device for NVIDIA");
      return TRUE;
    }
  }

  if (priv->in_type == MemoryType::CUDA)
    priv->transfer_type = TransferType::CUDA_TO_HIP;
  else
    priv->transfer_type = TransferType::HIP_TO_CUDA;

  return TRUE;
}
#else
static gboolean
gst_hip_memory_copy_ensure_device (GstHipMemoryCopy * self)
{
  return TRUE;
}
#endif

static gboolean
gst_hip_memory_copy_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;

  if (!priv->device) {
    GST_ERROR_OBJECT (self, "No availaable HIP device");
    return FALSE;
  }

  gst_caps_replace (&priv->in_caps, incaps);
  gst_caps_replace (&priv->out_caps, outcaps);

  if (!gst_video_info_from_caps (&priv->info, incaps)) {
    GST_ERROR_OBJECT (self, "Invalid input caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  priv->Reset (false);

  auto features = gst_caps_get_features (incaps, 0);
  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
    priv->in_type = MemoryType::HIP;
  }
#ifdef HAVE_GST_CUDA
  else if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    priv->in_type = MemoryType::CUDA;
  }
#endif

  features = gst_caps_get_features (outcaps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
    priv->out_type = MemoryType::HIP;
  }
#ifdef HAVE_GST_CUDA
  else if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    priv->out_type = MemoryType::CUDA;
  }
#endif

  priv->transfer_type = TransferType::SYSTEM;
  return gst_hip_memory_copy_ensure_device (self);
}

static gboolean
gst_hip_memory_copy_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    auto elem = GST_ELEMENT (trans);
    if (gst_hip_handle_context_query (elem, query, priv->device))
      return TRUE;

#ifdef HAVE_GST_CUDA
    if (gst_cuda_handle_context_query (elem, query, priv->cuda_ctx))
      return TRUE;
#endif
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static void
gst_hip_memory_copy_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;

  bool need_reconfigure = false;
  if (priv->transfer_type == TransferType::SYSTEM)
    return;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (priv->in_type == MemoryType::CUDA) {
    if (!gst_is_cuda_memory (mem)) {
      GST_WARNING_OBJECT (self, "Input memory is not cuda");
      priv->transfer_type = TransferType::SYSTEM;
      return;
    }

    auto cmem = GST_CUDA_MEMORY_CAST (mem);
    guint device_id = gst_hip_device_get_device_id (priv->device);
    guint cuda_dev_id;
    g_object_get (cmem->context, "cuda-device-id", &cuda_dev_id, nullptr);
    if (cuda_dev_id != device_id) {
      GST_INFO_OBJECT (self, "cuda device is updated");
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      gst_clear_object (&priv->cuda_ctx);
      priv->cuda_ctx = (GstCudaContext *) gst_object_ref (cmem->context);

      auto old_dev = priv->device;
      priv->device = nullptr;
      priv->target_vendor = GST_HIP_VENDOR_NVIDIA;
      priv->target_id = device_id;
      priv->search_type = DeviceSearchType::DEVICE_ID;
      auto ret =
          gst_hip_ensure_element_data (GST_ELEMENT (self), priv->target_vendor,
          priv->target_id, &priv->device);
      priv->search_type = DeviceSearchType::PROPERTY;
      if (!ret) {
        GST_WARNING_OBJECT (self, "Couldn't get hip device");
        priv->device = old_dev;
        priv->transfer_type = TransferType::SYSTEM;
        return;
      }

      gst_clear_object (&old_dev);
      need_reconfigure = true;
    }
  } else if (priv->in_type == MemoryType::HIP) {
    if (!gst_is_hip_memory (mem)) {
      GST_WARNING_OBJECT (self, "Input memory is not hip");
      priv->transfer_type = TransferType::SYSTEM;
      return;
    }

    auto hmem = GST_HIP_MEMORY_CAST (mem);
    if (!gst_hip_device_is_equal (hmem->device, priv->device)) {
      GST_INFO_OBJECT (self, "hip device is updated");
      std::lock_guard < std::recursive_mutex > lk (priv->lock);

      auto other_vendor = gst_hip_device_get_vendor (hmem->device);
      if (other_vendor != GST_HIP_VENDOR_NVIDIA) {
        GST_INFO_OBJECT (self, "Input is not NVIDIA");
        priv->transfer_type = TransferType::SYSTEM;
        return;
      }


      gst_clear_object (&priv->device);
      priv->device = (GstHipDevice *) gst_object_ref (hmem->device);

      auto new_dev_id = gst_hip_device_get_device_id (priv->device);
      gst_clear_object (&priv->cuda_ctx);

      priv->target_id = new_dev_id;
      priv->target_vendor = GST_HIP_VENDOR_NVIDIA;
      priv->search_type = DeviceSearchType::DEVICE_ID;
      auto ret = gst_cuda_ensure_element_context (GST_ELEMENT (self),
          priv->target_id, &priv->cuda_ctx);
      priv->search_type = DeviceSearchType::PROPERTY;
      if (!ret) {
        GST_WARNING_OBJECT (self, "Couldn't get cuda context");
        priv->transfer_type = TransferType::SYSTEM;
      }

      need_reconfigure = true;
    }
  }

  if (need_reconfigure) {
    GST_DEBUG_OBJECT (self, "Reconfiguring for device update");
    gst_hip_memory_copy_set_caps (trans, priv->in_caps, priv->out_caps);
    gst_base_transform_reconfigure_src (trans);
  }
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++) {
    gst_caps_set_features (tmp, i,
        gst_caps_features_new_single_static_str (feature_name));
  }

  return tmp;
}

static GstCaps *
gst_hip_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    if (priv->is_uploader) {
      auto caps_hip =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY);
      tmp = gst_caps_merge (caps_hip, gst_caps_ref (caps));
    } else {
      auto caps_sys =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
#ifdef HAVE_GST_CUDA
      auto caps_cuda =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);
      tmp = gst_caps_merge (caps_cuda, caps_sys);
      tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
#else
      tmp = gst_caps_merge (gst_caps_ref (caps), caps_sys);
#endif
    }
  } else {
    if (priv->is_uploader) {
      auto caps_sys =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
#ifdef HAVE_GST_CUDA
      auto caps_cuda =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);
      tmp = gst_caps_merge (caps_cuda, caps_sys);
      tmp = gst_caps_merge (tmp, gst_caps_ref (caps));
#else
      tmp = gst_caps_merge (caps_sys, gst_caps_ref (caps));
#endif
    } else {
      auto caps_hip =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY);
      tmp = gst_caps_merge (caps_hip, gst_caps_ref (caps));
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
gst_hip_memory_copy_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;
  GstVideoInfo info;
  GstBufferPool *pool = nullptr;
  GstCaps *caps;
  guint size;
  bool is_system = true;

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
    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
      GST_DEBUG_OBJECT (self, "upstream support hip memory");
      pool = gst_hip_buffer_pool_new (priv->device);
      is_system = false;
    }
#ifdef HAVE_GST_CUDA
    else if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY) && priv->cuda_ctx) {
      GST_DEBUG_OBJECT (self, "upstream support cuda memory");
      pool = gst_cuda_buffer_pool_new (priv->cuda_ctx);
      is_system = false;
    }
#endif

    if (!pool)
      pool = gst_video_buffer_pool_new ();

    auto config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (is_system) {
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
gst_hip_memory_copy_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;
  GstBufferPool *pool = nullptr;
  GstVideoInfo info;
  guint min, max, size;
  GstCaps *caps = nullptr;
  bool update_pool = false;

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
  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
    GST_DEBUG_OBJECT (self, "downstream support hip memory");
    if (pool) {
      if (!GST_IS_HIP_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto hpool = GST_HIP_BUFFER_POOL (pool);
        if (!gst_hip_device_is_equal (hpool->device, priv->device))
          gst_clear_object (&pool);
      }
    }

    if (!pool)
      pool = gst_hip_buffer_pool_new (priv->device);
  }
#ifdef HAVE_GST_CUDA
  else if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    GST_DEBUG_OBJECT (self, "downstream support cuda memory");
    if (pool) {
      if (!GST_IS_CUDA_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto cpool = GST_CUDA_BUFFER_POOL (pool);
        if (cpool->context != priv->cuda_ctx)
          gst_clear_object (&pool);
      }
    }

    if (!pool)
      pool = gst_cuda_buffer_pool_new (priv->cuda_ctx);
  }
#endif

  if (!pool)
    pool = gst_video_buffer_pool_new ();

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

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

static GstFlowReturn
gst_hip_memory_copy_system_copy (GstHipMemoryCopy * self,
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

#ifdef HAVE_GST_CUDA
static gboolean
gst_hip_memory_copy_device_copy (GstHipMemoryCopy * self, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto priv = self->priv;
  CUstream stream = nullptr;
  GstVideoFrame in_frame, out_frame;
  gboolean ret = TRUE;

  if (!gst_hip_device_set_current (priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't set device");
    return FALSE;
  }

  GstCudaMemory *cmem;
  if (priv->transfer_type == TransferType::CUDA_TO_HIP)
    cmem = (GstCudaMemory *) gst_buffer_peek_memory (inbuf, 0);
  else
    cmem = (GstCudaMemory *) gst_buffer_peek_memory (outbuf, 0);

  stream = gst_cuda_stream_get_handle (gst_cuda_memory_get_stream (cmem));

  if (!gst_video_frame_map (&in_frame, &priv->info, inbuf, GST_MAP_READ_HIP)) {
    GST_ERROR_OBJECT (self, "Couldn't map input frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, &priv->info, outbuf, GST_MAP_WRITE_HIP)) {
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    gst_video_frame_unmap (&in_frame);
    return FALSE;
  }

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&in_frame); i++) {
    auto in_data = (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&in_frame, i);
    auto out_data = (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    auto in_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, i);
    auto out_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, i);
    auto width_in_bytes = GST_VIDEO_FRAME_COMP_PSTRIDE (&in_frame, i) *
        GST_VIDEO_FRAME_COMP_WIDTH (&in_frame, i);
    auto height = GST_VIDEO_FRAME_COMP_HEIGHT (&in_frame, i);

    CUDA_MEMCPY2D param = { };

    param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    param.srcDevice = in_data;
    param.srcPitch = in_stride;

    param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    param.dstDevice = out_data;
    param.dstPitch = out_stride;
    param.WidthInBytes = width_in_bytes;
    param.Height = height;

    ret = gst_cuda_result (CuMemcpy2DAsync (&param, stream));
    if (!ret)
      break;
  }

  if (ret)
    ret = gst_cuda_result (CuStreamSynchronize (stream));

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;
}
#endif

static GstFlowReturn
gst_hip_memory_copy_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_HIP_MEMORY_COPY (trans);

#ifdef HAVE_GST_CUDA
  auto priv = self->priv;
  if (priv->transfer_type != TransferType::SYSTEM) {
    auto ret = gst_hip_memory_copy_device_copy (self, inbuf, outbuf);
    if (ret) {
      GST_TRACE_OBJECT (self, "Done using device copy");
      return GST_FLOW_OK;
    }

    priv->transfer_type = TransferType::SYSTEM;
  }
#endif

  return gst_hip_memory_copy_system_copy (self, inbuf, outbuf);
}

struct _GstHipUpload
{
  GstHipMemoryCopy parent;
};

G_DEFINE_TYPE (GstHipUpload, gst_hip_upload, GST_TYPE_HIP_MEMORY_COPY);

static void
gst_hip_upload_class_init (GstHipUploadClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "HIP Uploader", "Filter/Video",
      "Uploads system memory into HIP device memory",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_hip_upload_init (GstHipUpload * self)
{
  auto memcpy = GST_HIP_MEMORY_COPY (self);
  memcpy->priv->is_uploader = true;
}

struct _GstHipDownload
{
  GstHipMemoryCopy parent;
};

G_DEFINE_TYPE (GstHipDownload, gst_hip_download, GST_TYPE_HIP_MEMORY_COPY);

static void
gst_hip_download_class_init (GstHipDownloadClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "HIP Downloader", "Filter/Video",
      "Downloads HIP device memory into system memory",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_hip_download_init (GstHipDownload * self)
{
  auto memcpy = GST_HIP_MEMORY_COPY (self);
  memcpy->priv->is_uploader = false;
}
