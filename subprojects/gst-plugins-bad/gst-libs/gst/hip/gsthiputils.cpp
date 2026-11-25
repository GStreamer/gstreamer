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

  if (copy_type == CopyHtoD && hip_ret == hipSuccess) {
    GstHipEvent *event = nullptr;
    if (gst_hip_stream_record_event (stream, &event)) {
      auto frame_to_hold = g_new0 (GstVideoFrame, 1);
      *frame_to_hold = in_frame;

      gst_hip_memory_set_event (GST_HIP_MEMORY_CAST (out_mem), event);
      gst_hip_event_set_notify (event, frame_to_hold,[](gpointer user_data) {
            auto frame_to_hold = (GstVideoFrame *) user_data;
            gst_video_frame_unmap (frame_to_hold);
            g_free (frame_to_hold);
          }
      );

      gst_hip_event_unref (event);
      gst_video_frame_unmap (&out_frame);

      return TRUE;
    }

    auto sync_ret = HipStreamSynchronize (vendor, stream_handle);
    gst_hip_result (sync_ret, vendor);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&in_frame);
    return FALSE;
  }

  /* Also finish already submitted planes if a later copy failed */
  auto sync_ret = HipStreamSynchronize (vendor, stream_handle);
  gst_hip_result (sync_ret, vendor);
  if (hip_ret == hipSuccess)
    hip_ret = sync_ret;

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return gst_hip_result (hip_ret, vendor);
}

static gboolean
copy_buffer_bytes_cpu (GstBuffer * dest, gsize dest_offset, GstBuffer * src,
    gsize src_offset, gsize size)
{
  GstMapInfo src_map, dst_map;

  if (!gst_buffer_map (src, &src_map, GST_MAP_READ)) {
    GST_ERROR ("Couldn't map source buffer for CPU copy");
    return FALSE;
  }

  if (!gst_buffer_map (dest, &dst_map, GST_MAP_WRITE)) {
    GST_ERROR ("Couldn't map destination buffer for CPU copy");
    gst_buffer_unmap (src, &src_map);
    return FALSE;
  }

  memcpy (dst_map.data + dest_offset, src_map.data + src_offset, size);
  gst_buffer_unmap (dest, &dst_map);
  gst_buffer_unmap (src, &src_map);

  GST_LOG ("Copied %" G_GSIZE_FORMAT " bytes on the CPU", size);

  return TRUE;
}

static GstHipDevice *
get_copy_memory_device (GstMemory * mem)
{
  if (gst_is_hip_memory (mem))
    return GST_HIP_MEMORY_CAST (mem)->device;

  if (gst_is_hip_host_memory (mem))
    return GST_HIP_HOST_MEMORY_CAST (mem)->device;

  return nullptr;
}

/**
 * gst_hip_buffer_copy_bytes_into:
 * @dest: a writable destination #GstBuffer
 * @dest_offset: byte offset from the start of @dest
 * @src: source #GstBuffer
 * @src_offset: byte offset from the start of @src
 * @size: number of bytes to copy
 *
 * Copies a byte range. See gst_hip_buffer_copy_bytes_into_full().
 *
 * Returns: %TRUE if the copy completed
 *
 * Since: 1.30
 */
gboolean
gst_hip_buffer_copy_bytes_into (GstBuffer * dest, gsize dest_offset,
    GstBuffer * src, gsize src_offset, gsize size)
{
  return gst_hip_buffer_copy_bytes_into_full (dest, dest_offset, src,
      src_offset, size, nullptr);
}

/**
 * gst_hip_buffer_copy_bytes_into_full:
 * @dest: a writable destination #GstBuffer
 * @dest_offset: byte offset from the start of @dest
 * @src: source #GstBuffer
 * @src_offset: byte offset from the start of @src
 * @size: number of bytes to copy
 * @stream: (nullable): a #GstHipStream, or %NULL to use the memory's stream
 *
 * Copies between HIP device or pinned host memories on the same device.
 * Waits for the copy to complete before returning. Falls back to a CPU
 * copy when the buffers cannot use the HIP copy path. The byte ranges
 * must fit within their buffers and must not overlap.
 *
 * Returns: %TRUE if the copy completed
 *
 * Since: 1.30
 */
gboolean
gst_hip_buffer_copy_bytes_into_full (GstBuffer * dest, gsize dest_offset,
    GstBuffer * src, gsize src_offset, gsize size, GstHipStream * stream)
{
  g_return_val_if_fail (GST_IS_BUFFER (dest), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src), FALSE);

  if (!size || dest == src) {
    GST_DEBUG ("Expected a nonempty byte range and distinct buffers");
    return FALSE;
  }

  auto dst_size = gst_buffer_get_size (dest);
  auto src_size = gst_buffer_get_size (src);
  if (dest_offset > dst_size || size > dst_size - dest_offset ||
      src_offset > src_size || size > src_size - src_offset) {
    GST_DEBUG ("Copy of %" G_GSIZE_FORMAT " bytes exceeds buffer bounds "
        "(src offset %" G_GSIZE_FORMAT ", size %" G_GSIZE_FORMAT
        ", dst offset %" G_GSIZE_FORMAT ", size %" G_GSIZE_FORMAT ")",
        size, src_offset, src_size, dest_offset, dst_size);
    return FALSE;
  }

  if (gst_buffer_n_memory (dest) != 1 || gst_buffer_n_memory (src) != 1) {
    GST_DEBUG ("Expected one memory per buffer for HIP copy");
    return copy_buffer_bytes_cpu (dest, dest_offset, src, src_offset, size);
  }

  auto src_mem = gst_buffer_peek_memory (src, 0);
  auto dst_mem = gst_buffer_peek_memory (dest, 0);
  auto src_device = get_copy_memory_device (src_mem);
  auto dst_device = get_copy_memory_device (dst_mem);
  auto src_hip = gst_is_hip_memory (src_mem);
  auto dst_hip = gst_is_hip_memory (dst_mem);

  if ((!src_hip && !dst_hip) || !src_device || !dst_device ||
      !gst_hip_device_is_equal (src_device, dst_device)) {
    GST_DEBUG ("Buffers do not contain compatible HIP memories");
    return copy_buffer_bytes_cpu (dest, dest_offset, src, src_offset, size);
  }

  auto device = src_hip ? src_device : dst_device;
  auto vendor = gst_hip_device_get_vendor (device);
  if (!stream) {
    auto mem = src_hip ? src_mem : dst_mem;
    stream = gst_hip_memory_get_stream (GST_HIP_MEMORY_CAST (mem));
    if (!stream)
      stream = gst_hip_device_get_stream (device);
  }

  if (gst_hip_stream_get_vendor (stream) != vendor ||
      gst_hip_stream_get_device_id (stream) !=
      gst_hip_device_get_device_id (device)) {
    GST_ERROR_OBJECT (device, "Copy stream belongs to another device");
    return FALSE;
  }

  GstMapInfo src_map, dst_map;
  if (!gst_memory_map (src_mem, &src_map,
          src_hip ? GST_MAP_READ_HIP : GST_MAP_READ)) {
    GST_ERROR_OBJECT (device, "Couldn't map source memory for HIP copy");
    return FALSE;
  }

  if (!gst_memory_map (dst_mem, &dst_map,
          dst_hip ? GST_MAP_WRITE_HIP : GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (device, "Couldn't map destination memory for HIP copy");
    gst_memory_unmap (src_mem, &src_map);
    return FALSE;
  }

  gboolean ret = FALSE;
  if (gst_hip_device_set_current (device) &&
      gst_hip_stream_schedule_wait_buffer (stream, src) &&
      gst_hip_stream_schedule_wait_buffer (stream, dest)) {
    auto src_data = src_map.data + src_offset;
    auto dst_data = dst_map.data + dest_offset;
    auto handle = gst_hip_stream_get_handle (stream);
    hipError_t status;

    if (src_hip && dst_hip) {
      status = HipMemcpyDtoDAsync (vendor, dst_data, src_data, size, handle);
    } else if (src_hip) {
      status = HipMemcpyDtoHAsync (vendor, dst_data, src_data, size, handle);
    } else {
      status = HipMemcpyHtoDAsync (vendor, dst_data, src_data, size, handle);
    }

    if (gst_hip_result (status, vendor))
      ret = gst_hip_stream_synchronize (stream);
  }

  gst_memory_unmap (dst_mem, &dst_map);
  gst_memory_unmap (src_mem, &src_map);

  if (ret) {
    GST_LOG_OBJECT (device, "Copied %" G_GSIZE_FORMAT " bytes on stream %p "
        "(src offset %" G_GSIZE_FORMAT ", dst offset %" G_GSIZE_FORMAT ")",
        size, stream, src_offset, dest_offset);
  } else {
    GST_ERROR_OBJECT (device, "Couldn't copy buffer bytes on HIP stream");
  }

  return ret;
}

/**
 * gst_hip_buffer_set_event:
 * @buffer: a #GstBuffer
 * @event: (transfer none) (nullable): a #GstHipEvent
 *
 * Sets @event on each HIP memory in @buffer. Non-HIP memories are ignored.
 *
 * Since: 1.30
 */
void
gst_hip_buffer_set_event (GstBuffer * buffer, GstHipEvent * event)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));

  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    if (gst_is_hip_memory (mem))
      gst_hip_memory_set_event ((GstHipMemory *) mem, event);
  }
}

/**
 * gst_hip_stream_schedule_wait_buffer:
 * @stream: a #GstHipStream
 * @buffer: a #GstBuffer
 *
 * Schedules GPU-side waits for events attached to HIP memories in @buffer.
 * Non-HIP memories and memories without an event are ignored.
 * This function does not wait on the CPU.
 *
 * Returns: %TRUE if all required waits were scheduled successfully
 *
 * Since: 1.30
 */
gboolean
gst_hip_stream_schedule_wait_buffer (GstHipStream * stream, GstBuffer * buffer)
{
  g_return_val_if_fail (stream, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    if (!gst_is_hip_memory (mem))
      continue;

    GstHipEvent *event = nullptr;
    if (!gst_hip_memory_get_event ((GstHipMemory *) mem, &event))
      continue;

    auto ret = gst_hip_stream_wait_event (stream, event);
    gst_hip_event_unref (event);

    if (!ret)
      return FALSE;
  }

  return TRUE;
}
