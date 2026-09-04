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
#include "config.h"
#endif

#include "gsthip.h"
#include <mutex>
#include <gmodule.h>
#include "gsthiputils-private.h"

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hiputils", 0, "hiputils");
      });

  return cat;
}
#endif

/*
 * Note: this function's usage of g_dir_read_name() on Win32 is inefficient
 * because of UTF16-UTF8 conversions, so it cannot be used in directories with
 * lots of files like C:\Windows\System32. Should be changed to
 * `FindFirstFileEx()` etc if that becomes needed.
 */
GModule *
load_hiplib_from_root (const char *hip_root, const char *subdir,
    const char *prefix, const char *suffix)
{
  GModule *module = nullptr;
  char *path = g_build_path (G_DIR_SEPARATOR_S, hip_root, subdir, nullptr);
  GDir *dir = g_dir_open (path, 0, nullptr);
  if (dir) {
    const gchar *name;
    while ((name = g_dir_read_name (dir))) {
      if (g_str_has_prefix (name, prefix) && g_str_has_suffix (name, suffix)) {
        char *lib_path = g_build_filename (path, name, nullptr);
        module = g_module_open (lib_path, G_MODULE_BIND_LAZY);
        GST_INFO ("Loaded %s", lib_path);
        g_free (lib_path);
        break;
      }
    }
    g_dir_close (dir);
  }
  g_free (path);
  return module;
}

gboolean
_gst_hip_result (hipError_t result, GstHipVendor vendor, GstDebugCategory * cat,
    const gchar * file, const gchar * function, gint line)
{
  if (result != hipSuccess) {
#ifndef GST_DISABLE_GST_DEBUG
    if (vendor != GST_HIP_VENDOR_UNKNOWN) {
      auto error_name = HipGetErrorName (vendor, result);
      auto error_str = HipGetErrorString (vendor, result);
      gst_debug_log (cat, GST_LEVEL_ERROR, file, function, line,
          NULL, "HIP call failed: %s, %s", error_name, error_str);
    }
#endif
    return FALSE;
  }

  return TRUE;
}

static void
context_set_hip_device (GstContext * context, GstHipDevice * device)
{
  g_return_if_fail (context != nullptr);

  guint device_id;
  GstHipVendor vendor;
  g_object_get (device, "device-id", &device_id, "vendor", &vendor, nullptr);

  auto s = gst_context_writable_structure (context);
  gst_structure_set (s, "device", GST_TYPE_HIP_DEVICE, device,
      "vendor", GST_TYPE_HIP_VENDOR, vendor,
      "device-id", G_TYPE_UINT, device_id, nullptr);

#ifdef G_OS_WIN32
  gint64 luid = 0;
  g_object_get (device, "adapter-luid", &luid, nullptr);
  gst_structure_set (s, "adapter-luid", G_TYPE_INT64, luid, nullptr);
#else
  gchar *pci_bus_id = nullptr;
  g_object_get (device, "pci-bus-id", &pci_bus_id, nullptr);
  if (pci_bus_id)
    gst_structure_set (s, "pci-bus-id", G_TYPE_STRING, pci_bus_id, nullptr);
  g_free (pci_bus_id);
#endif
}

static gboolean
pad_query (const GValue * item, GValue * value, gpointer user_data)
{
  GstPad *pad = (GstPad *) g_value_get_object (item);
  GstQuery *query = (GstQuery *) user_data;
  gboolean res;

  res = gst_pad_peer_query (pad, query);
  if (res) {
    g_value_set_boolean (value, TRUE);
    return FALSE;
  }

  return TRUE;
}

static gboolean
run_query (GstElement * element, GstQuery * query, GstPadDirection direction)
{
  GstIterator *it;
  GstIteratorFoldFunction func = pad_query;
  GValue res = G_VALUE_INIT;

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

  /* Ask neighbor */
  if (direction == GST_PAD_SRC)
    it = gst_element_iterate_src_pads (element);
  else
    it = gst_element_iterate_sink_pads (element);

  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);

  gst_iterator_free (it);

  return g_value_get_boolean (&res);
}

static void
run_hip_context_query (GstElement * element, GstHipDevice ** device)
{
  GstQuery *query;
  GstContext *ctx = nullptr;

  query = gst_query_new_context (GST_HIP_DEVICE_CONTEXT_TYPE);
  if (run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctx);
    if (ctx)
      gst_element_set_context (element, ctx);
  }

  if (*device == nullptr && run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctx);
    if (ctx)
      gst_element_set_context (element, ctx);
  }

  if (*device == nullptr) {
    auto msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        GST_HIP_DEVICE_CONTEXT_TYPE);
    gst_element_post_message (element, msg);
  }

  gst_query_unref (query);
}

/**
 * gst_hip_ensure_element_data:
 * @element: the #GstElement running the query
 * @vendor: a #GstHipVendor
 * @device_id: preferred device-id, pass device_id >=0 when
 *             the device_id explicitly required. Otherwise, set -1.
 * @device: (inout): the resulting #GstHipDevice
 *
 * Perform the steps necessary for retrieving a #GstHipDevice from the
 * surrounding elements or from the application using the #GstContext mechanism.
 *
 * If the content of @device is not %NULL, then no #GstContext query is
 * necessary for #GstHipDevice.
 *
 * Returns: whether a #GstHipDevice exists in @device
 *
 * Since: 1.28
 */
gboolean
gst_hip_ensure_element_data (GstElement * element, GstHipVendor vendor,
    gint device_id, GstHipDevice ** device)
{
  if (*device)
    return TRUE;

  run_hip_context_query (element, device);
  if (*device)
    return TRUE;

  guint target_device_id = 0;
  if (device_id > 0)
    target_device_id = device_id;

  *device = gst_hip_device_new (vendor, target_device_id);

  if (*device == nullptr) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with device id %d", target_device_id);
    return FALSE;
  } else {
    auto ctx = gst_context_new_hip_device (*device);
    gst_element_set_context (element, ctx);
    auto msg = gst_message_new_have_context (GST_OBJECT_CAST (element), ctx);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

/**
 * gst_hip_ensure_element_data_for_adapter_luid:
 * @element: the #GstElement running the query
 * @vendor: a #GstHipVendor
 * @adapter_luid: DXGI adapter LUID
 * @device: (inout): the resulting #GstHipDevice
 *
 * Perform the steps necessary for retrieving a #GstHipDevice from the
 * surrounding elements or from the application using the #GstContext mechanism.
 *
 * If the content of @device is not %NULL, then no #GstContext query is
 * necessary for #GstHipDevice.
 *
 * Returns: whether a #GstHipDevice exists in @device
 *
 * Since: 1.30
 */
gboolean
gst_hip_ensure_element_data_for_adapter_luid (GstElement * element,
    GstHipVendor vendor, gint64 adapter_luid, GstHipDevice ** device)
{
  if (*device)
    return TRUE;

  run_hip_context_query (element, device);
  if (*device)
    return TRUE;

  *device = gst_hip_device_new_for_adapter_luid (vendor, adapter_luid);

  if (*device == nullptr) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with luid %" G_GINT64_FORMAT, adapter_luid);
    return FALSE;
  } else {
    auto ctx = gst_context_new_hip_device (*device);
    gst_element_set_context (element, ctx);
    auto msg = gst_message_new_have_context (GST_OBJECT_CAST (element), ctx);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

/**
 * gst_hip_ensure_element_data_for_pci_bus_id:
 * @element: the #GstElement running the query
 * @vendor: a #GstHipVendor
 * @pci_bus_id: The PCI bus ID
 * @device: (inout): the resulting #GstHipDevice
 *
 * Perform the steps necessary for retrieving a #GstHipDevice from the
 * surrounding elements or from the application using the #GstContext mechanism.
 *
 * If the content of @device is not %NULL, then no #GstContext query is
 * necessary for #GstHipDevice.
 *
 * Returns: whether a #GstHipDevice exists in @device
 *
 * Since: 1.30
 */
gboolean
gst_hip_ensure_element_data_for_pci_bus_id (GstElement * element,
    GstHipVendor vendor, const gchar * pci_bus_id, GstHipDevice ** device)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (pci_bus_id, FALSE);
  g_return_val_if_fail (device, FALSE);

  if (*device)
    return TRUE;

  run_hip_context_query (element, device);
  if (*device)
    return TRUE;

  *device = gst_hip_device_new_for_pci_bus_id (vendor, pci_bus_id);

  if (*device == nullptr) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with PCI bus ID %s", pci_bus_id);
    return FALSE;
  } else {
    auto ctx = gst_context_new_hip_device (*device);
    gst_element_set_context (element, ctx);
    auto msg = gst_message_new_have_context (GST_OBJECT_CAST (element), ctx);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

/**
 * gst_hip_handle_set_context:
 * @element: a #GstElement
 * @context: a #GstContext
 * @vendor: a #GstHipVendor
 * @device_id: preferred device-id, pass device_id >=0 when
 *             the device_id explicitly required. Otherwise, set -1.
 * @device: (inout) (transfer full): location of a #GstHipDevice
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * HIP capable elements.
 *
 * Retrieves the #GstHipDevice in @context and places the result in @device.
 *
 * Returns: whether the @device could be set successfully
 *
 * Since: 1.28
 */
gboolean
gst_hip_handle_set_context (GstElement * element, GstContext * context,
    GstHipVendor vendor, gint device_id, GstHipDevice ** device)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != nullptr, FALSE);

  if (!context)
    return FALSE;

  auto context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_HIP_DEVICE_CONTEXT_TYPE) == 0) {
    GstHipDevice *other_device = nullptr;
    guint other_idx = 0;
    GstHipVendor other_vendor;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    auto s = gst_context_get_structure (context);
    if (gst_structure_get (s, "device", GST_TYPE_HIP_DEVICE, &other_device,
            "vendor", GST_TYPE_HIP_VENDOR, &other_vendor,
            "device-id", G_TYPE_UINT, &other_idx, nullptr)) {
      if ((device_id == -1 || (guint) device_id == other_idx) &&
          (vendor == GST_HIP_VENDOR_UNKNOWN || vendor == other_vendor)) {
        *device = other_device;
        return TRUE;
      }

      gst_object_unref (other_device);
    }
  }

  return FALSE;
}

/**
 * gst_hip_handle_set_context_for_adapter_luid:
 * @element: a #GstElement
 * @context: a #GstContext
 * @vendor: a #GstHipVendor
 * @adapter_luid: DXGI adapter LUID
 * @device: (inout) (transfer full): location of a #GstHipDevice
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * HIP capable elements.
 *
 * Retrieves the #GstHipDevice in @context and places the result in @device.
 *
 * Returns: whether the @device could be set successfully
 *
 * Since: 1.30
 */
gboolean
gst_hip_handle_set_context_for_adapter_luid (GstElement * element,
    GstContext * context, GstHipVendor vendor, gint64 adapter_luid,
    GstHipDevice ** device)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != nullptr, FALSE);

  if (!context)
    return FALSE;

  auto context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_HIP_DEVICE_CONTEXT_TYPE) == 0) {
    GstHipDevice *other_device = nullptr;
    gint64 other_adapter = 0;
    GstHipVendor other_vendor;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    auto s = gst_context_get_structure (context);
    if (gst_structure_get (s, "device", GST_TYPE_HIP_DEVICE, &other_device,
            "vendor", GST_TYPE_HIP_VENDOR, &other_vendor,
            "adapter-luid", G_TYPE_INT64, &other_adapter, nullptr)) {
      if (adapter_luid == other_adapter &&
          (vendor == GST_HIP_VENDOR_UNKNOWN || vendor == other_vendor)) {
        *device = other_device;
        return TRUE;
      }

      gst_object_unref (other_device);
    }
  }

  return FALSE;
}

/**
 * gst_hip_handle_set_context_for_pci_bus_id:
 * @element: a #GstElement
 * @context: a #GstContext
 * @vendor: a #GstHipVendor
 * @pci_bus_id: The PCI bus ID
 * @device: (inout) (transfer full): location of a #GstHipDevice
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * HIP capable elements.
 *
 * Retrieves the #GstHipDevice in @context and places the result in @device.
 *
 * Returns: whether the @device could be set successfully
 *
 * Since: 1.30
 */
gboolean
gst_hip_handle_set_context_for_pci_bus_id (GstElement * element,
    GstContext * context, GstHipVendor vendor, const gchar * pci_bus_id,
    GstHipDevice ** device)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device, FALSE);
  g_return_val_if_fail (pci_bus_id, FALSE);

  if (!context)
    return FALSE;

  auto context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_HIP_DEVICE_CONTEXT_TYPE) == 0) {
    GstHipDevice *other_device = nullptr;
    gchar *other_pci_bus_id = nullptr;
    GstHipVendor other_vendor;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    auto s = gst_context_get_structure (context);
    if (gst_structure_get (s, "device", GST_TYPE_HIP_DEVICE, &other_device,
            "vendor", GST_TYPE_HIP_VENDOR, &other_vendor,
            "pci-bus-id", G_TYPE_STRING, &other_pci_bus_id, nullptr)) {
      if (g_strcmp0 (pci_bus_id, other_pci_bus_id) == 0 &&
          (vendor == GST_HIP_VENDOR_UNKNOWN || vendor == other_vendor)) {
        *device = other_device;
        g_free (other_pci_bus_id);
        return TRUE;
      }

      gst_object_unref (other_device);
      g_free (other_pci_bus_id);
    }
  }

  return FALSE;
}

/**
 * gst_hip_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type %GST_QUERY_CONTEXT
 * @device: (transfer none) (nullable): a #GstHipDevice
 *
 * Returns: Whether the @query was successfully responded to from the passed
 *          @context.
 *
 * Since: 1.28
 */
gboolean
gst_hip_handle_context_query (GstElement * element, GstQuery * query,
    GstHipDevice * device)
{
  const gchar *context_type;
  GstContext *context;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  if (!GST_IS_HIP_DEVICE (device))
    return FALSE;

  gst_query_parse_context_type (query, &context_type);
  if (g_strcmp0 (context_type, GST_HIP_DEVICE_CONTEXT_TYPE) != 0)
    return FALSE;

  GstContext *old_ctx = nullptr;
  gst_query_parse_context (query, &old_ctx);
  if (old_ctx)
    context = gst_context_copy (old_ctx);
  else
    context = gst_context_new (GST_HIP_DEVICE_CONTEXT_TYPE, TRUE);

  context_set_hip_device (context, device);
  gst_query_set_context (query, context);
  gst_context_unref (context);

  GST_DEBUG_OBJECT (element, "successfully set %" GST_PTR_FORMAT
      " on %" GST_PTR_FORMAT, device, query);

  return TRUE;
}

/**
 * gst_context_new_hip_device:
 * @device: (transfer none): a #GstHipDevice
 *
 * Returns: (transfer full): a new #GstContext embedding the @device
 *
 * Since: 1.28
 */
GstContext *
gst_context_new_hip_device (GstHipDevice * device)
{
  g_return_val_if_fail (GST_HIP_DEVICE (device), nullptr);

  auto ctx = gst_context_new (GST_HIP_DEVICE_CONTEXT_TYPE, TRUE);
  context_set_hip_device (ctx, device);

  return ctx;
}

static gboolean
gst_hip_buffer_copy_into_fallback (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, info, src, GST_MAP_READ)) {
    GST_ERROR ("Couldn't map src frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, info, dst, GST_MAP_WRITE)) {
    GST_ERROR ("Couldn't map dst frame");
    gst_video_frame_unmap (&in_frame);
    return FALSE;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;
}

/**
 * gst_hip_buffer_copy_into:
 * @dest: a #GstBuffer
 * @src: a #GstBuffer
 * @info: a #GstVideoInfo
 *
 * Copies video data from @src into @dest according to @info.
 *
 * This function copies only memory contents and does not copy buffer
 * metadata. Use gst_buffer_copy_into() separately if metadata also needs
 * to be copied.
 *
 * If either @src or @dest contains HIP memory, an optimized copy path is
 * used when possible.
 *
 * Since: 1.30
 */
gboolean
gst_hip_buffer_copy_into (GstBuffer * dest, GstBuffer * src,
    const GstVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_BUFFER (dest), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src), FALSE);
  g_return_val_if_fail (info, FALSE);

  /* HIP expects single memory buffer */
  if (gst_buffer_n_memory (src) != 1 || gst_buffer_n_memory (dest) != 1)
    return gst_hip_buffer_copy_into_fallback (dest, src, info);

  auto in_mem = gst_buffer_peek_memory (src, 0);
  auto out_mem = gst_buffer_peek_memory (dest, 0);

  auto in_hip = gst_is_hip_memory (in_mem);
  auto out_hip = gst_is_hip_memory (out_mem);

  if (!in_hip && !out_hip)
    return gst_hip_buffer_copy_into_fallback (dest, src, info);

  enum CopyType
  {
    CopyUnknown,
    CopyDtoD,
    CopyDtoH,
    CopyHtoD,
  };

  CopyType copy_type = CopyUnknown;
  GstHipVendor vendor = GST_HIP_VENDOR_UNKNOWN;
  GstHipStream *stream = nullptr;

  if (in_hip) {
    auto in_hmem = GST_HIP_MEMORY_CAST (in_mem);
    stream = gst_hip_memory_get_stream (in_hmem);
    vendor = gst_hip_device_get_vendor (in_hmem->device);
    if (!stream)
      stream = gst_hip_device_get_stream (in_hmem->device);

    if (!out_hip) {
      copy_type = CopyDtoH;
    } else {
      auto out_hmem = GST_HIP_MEMORY_CAST (out_mem);
      if (gst_hip_device_is_equal (in_hmem->device, out_hmem->device)) {
        /* in/out same device, DtoD */
        copy_type = CopyDtoD;
      } else {
        /* Copy in device into out staging */
        copy_type = CopyDtoH;
      }
    }
  } else {
    auto out_hmem = GST_HIP_MEMORY_CAST (out_mem);
    stream = gst_hip_memory_get_stream (out_hmem);
    vendor = gst_hip_device_get_vendor (out_hmem->device);
    if (!stream)
      stream = gst_hip_device_get_stream (out_hmem->device);

    copy_type = CopyHtoD;
  }

  g_assert (copy_type != CopyUnknown);

  GstVideoFrame in_frame, out_frame;
  GstMapFlags in_map_flags, out_map_flags;

  switch (copy_type) {
    case CopyDtoD:
      in_map_flags = GST_MAP_READ_HIP;
      out_map_flags = GST_MAP_WRITE_HIP;
      break;
    case CopyDtoH:
      in_map_flags = GST_MAP_READ_HIP;
      out_map_flags = GST_MAP_WRITE;
      break;
    case CopyHtoD:
      in_map_flags = GST_MAP_READ;
      out_map_flags = GST_MAP_WRITE_HIP;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  if (!gst_video_frame_map (&in_frame, info, src, in_map_flags)) {
    GST_ERROR ("Couldn't map src frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, info, dest, out_map_flags)) {
    GST_ERROR ("Couldn't map dst frame");
    gst_video_frame_unmap (&in_frame);
    return FALSE;
  }

  hipError_t hip_ret = hipSuccess;
  auto stream_handle = gst_hip_stream_get_handle (stream);

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&in_frame); i++) {
    hip_Memcpy2D param = { };
    param.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, i);

    param.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, i);
    param.WidthInBytes = GST_VIDEO_FRAME_COMP_WIDTH (&in_frame, i)
        * GST_VIDEO_FRAME_COMP_PSTRIDE (&in_frame, i);
    param.Height = GST_VIDEO_FRAME_COMP_HEIGHT (&in_frame, i);

    if (copy_type == CopyDtoD) {
      param.srcMemoryType = hipMemoryTypeDevice;
      param.srcDevice = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, i);

      param.dstMemoryType = hipMemoryTypeDevice;
      param.dstDevice = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    } else if (copy_type == CopyDtoH) {
      param.srcMemoryType = hipMemoryTypeDevice;
      param.srcDevice = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, i);

      param.dstMemoryType = hipMemoryTypeHost;
      param.dstHost = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    } else {
      param.srcMemoryType = hipMemoryTypeHost;
      param.srcHost = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, i);

      param.dstMemoryType = hipMemoryTypeDevice;
      param.dstDevice = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    }

    hip_ret = HipMemcpyParam2DAsync (vendor, &param, stream_handle);
    if (!gst_hip_result (hip_ret, vendor))
      break;
  }

  if (hip_ret == hipSuccess)
    hip_ret = HipStreamSynchronize (vendor, stream_handle);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return gst_hip_result (hip_ret, vendor);
}
