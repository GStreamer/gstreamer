/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12.h"
#include "gstd3d12-private.h"
#include <mutex>
#include <atomic>

/* *INDENT-OFF* */
static std::recursive_mutex context_lock_;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12allocator", 0, "d3d12allocator");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif

static void
init_context_debug (void)
{
  GST_D3D12_CALL_ONCE_BEGIN {
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
  } GST_D3D12_CALL_ONCE_END;
}

/**
 * gst_d3d12_handle_set_context:
 * @element: a #GstElement
 * @context: a #GstContext
 * @adapter_index: a DXGI adapter index
 * @device: (inout) (transfer full): location of a #GstD3D12Device
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * D3D12 capable elements.
 *
 * Retrieve's the #GstD3D12Device in @context and places the result in @device.
 * @device is accepted if @adapter_index is equal to -1 (accept any device)
 * or equal to that of @device
 *
 * Returns: whether the @device could be set successfully
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_handle_set_context (GstElement * element, GstContext * context,
    gint adapter_index, GstD3D12Device ** device)
{
  const gchar *context_type;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != nullptr, FALSE);

  init_context_debug ();

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE) == 0) {
    const GstStructure *str;
    GstD3D12Device *other_device = nullptr;
    guint other_adapter = 0;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    str = gst_context_get_structure (context);

    if (gst_structure_get (str, "device", GST_TYPE_D3D12_DEVICE,
            &other_device, "adapter-index", G_TYPE_UINT, &other_adapter,
            nullptr)) {
      if (adapter_index == -1 || (guint) adapter_index == other_adapter) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_CONTEXT,
            element, "Found D3D12 device context");
        *device = other_device;

        return TRUE;
      }

      gst_object_unref (other_device);
    }
  }

  return FALSE;
}

/**
 * gst_d3d12_handle_set_context_for_adapter_luid:
 * @element: a #GstElement
 * @context: a #GstContext
 * @adapter_luid: an int64 representation of DXGI adapter LUID
 * @device: (inout) (transfer full): location of a #GstD3D12Device
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * D3D12 capable elements.
 *
 * Retrieve's the #GstD3D12Device in @context and places the result in @device.
 * @device is accepted only when @adapter_index is equal to that of @device
 *
 * Returns: whether the @device could be set successfully
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_handle_set_context_for_adapter_luid (GstElement * element,
    GstContext * context, gint64 adapter_luid, GstD3D12Device ** device)
{
  const gchar *context_type;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != nullptr, FALSE);

  init_context_debug ();

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE) == 0) {
    const GstStructure *str;
    GstD3D12Device *other_device = nullptr;
    gint64 other_adapter = 0;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    str = gst_context_get_structure (context);

    if (gst_structure_get (str, "device", GST_TYPE_D3D12_DEVICE,
            &other_device, "adapter-luid", G_TYPE_INT64, &other_adapter,
            nullptr)) {
      if (adapter_luid == other_adapter) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_CONTEXT,
            element, "Found D3D12 device context");
        *device = other_device;

        return TRUE;
      }

      gst_object_unref (other_device);
    }
  }

  return FALSE;
}

static void
context_set_d3d12_device (GstContext * context, GstD3D12Device * device)
{
  GstStructure *s;
  guint adapter_index = 0;
  guint device_id = 0;
  guint vendor_id = 0;
  gchar *desc = nullptr;
  gint64 adapter_luid = 0;

  g_return_if_fail (context != nullptr);

  g_object_get (G_OBJECT (device), "adapter-index", &adapter_index,
      "device-id", &device_id, "vendor-id", &vendor_id, "description", &desc,
      "adapter-luid", &adapter_luid, nullptr);

  GST_CAT_LOG (GST_CAT_CONTEXT,
      "setting GstD3D12Device(%" GST_PTR_FORMAT
      ") with adapter index %d on context(%" GST_PTR_FORMAT ")",
      device, adapter_index, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, "device", GST_TYPE_D3D12_DEVICE, device,
      "adapter-index", G_TYPE_UINT, adapter_index,
      "adapter-luid", G_TYPE_INT64, adapter_luid,
      "device-id", G_TYPE_UINT, device_id,
      "vendor-id", G_TYPE_UINT, vendor_id,
      "description", G_TYPE_STRING, GST_STR_NULL (desc), nullptr);
  g_free (desc);
}

/**
 * gst_d3d12_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type %GST_QUERY_CONTEXT
 * @device: (transfer none) (nullable): a #GstD3D12Device
 *
 * Returns: Whether the @query was successfully responded to from the passed
 * @device.
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_handle_context_query (GstElement * element, GstQuery * query,
    GstD3D12Device * device)
{
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  init_context_debug ();

  GST_LOG_OBJECT (element, "handle context query %" GST_PTR_FORMAT, query);

  if (!device)
    return FALSE;

  gst_query_parse_context_type (query, &context_type);
  if (g_strcmp0 (context_type, GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE) != 0)
    return FALSE;

  gst_query_parse_context (query, &old_context);
  if (old_context)
    context = gst_context_copy (old_context);
  else
    context = gst_context_new (GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);

  context_set_d3d12_device (context, device);
  gst_query_set_context (query, context);
  gst_context_unref (context);

  GST_DEBUG_OBJECT (element, "successfully set %" GST_PTR_FORMAT
      " on %" GST_PTR_FORMAT, device, query);

  return TRUE;
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

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, pad, "pad peer query failed");
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
run_d3d12_context_query (GstElement * element, GstD3D12Device ** device)
{
  GstQuery *query;
  GstContext *ctxt = nullptr;

  query = gst_query_new_context (GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE);
  if (run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    if (ctxt) {
      GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
          "found context (%" GST_PTR_FORMAT ") in downstream query", ctxt);
      gst_element_set_context (element, ctxt);
    }
  }

  if (*device == nullptr && run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    if (ctxt) {
      GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
          "found context (%" GST_PTR_FORMAT ") in upstream query", ctxt);
      gst_element_set_context (element, ctxt);
    }
  }

  if (*device == nullptr) {
    GstMessage *msg;

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting need context message");
    msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE);
    gst_element_post_message (element, msg);
  }

  gst_query_unref (query);
}

/**
 * gst_d3d12_ensure_element_data:
 * @element: the #GstElement running the query
 * @adapter: preferred DXGI adapter index, pass adapter >=0 when
 *           the adapter explicitly required. Otherwise, set -1.
 * @device: (inout): the resulting #GstD3D12Device
 *
 * Perform the steps necessary for retrieving a #GstD3D12Device
 * from the surrounding elements or from the application using the #GstContext mechanism.
 *
 * If the contents of @device is not %NULL, then no #GstContext query is
 * necessary for #GstD3D12Device retrieval is performed.
 *
 * Returns: whether a #GstD3D12Device exists in @device
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_ensure_element_data (GstElement * element, gint adapter_index,
    GstD3D12Device ** device)
{
  guint target_adapter = 0;
  std::lock_guard < std::recursive_mutex > lk (context_lock_);

  g_return_val_if_fail (element != nullptr, FALSE);
  g_return_val_if_fail (device != nullptr, FALSE);

  init_context_debug ();

  if (*device) {
    GST_LOG_OBJECT (element, "already have a device %" GST_PTR_FORMAT, *device);
    return TRUE;
  }

  run_d3d12_context_query (element, device);
  if (*device)
    return TRUE;

  if (adapter_index > 0)
    target_adapter = adapter_index;

  *device = gst_d3d12_device_new (target_adapter);

  if (*device == nullptr) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with adapter index %d", target_adapter);
    return FALSE;
  } else {
    GstContext *context;
    GstMessage *msg;

    /* Propagate new D3D12 device context */

    context = gst_context_new (GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);
    context_set_d3d12_device (context, *device);

    gst_element_set_context (element, context);

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting have context (%p) message with D3D12 device context (%p)",
        context, *device);
    msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

/**
 * gst_d3d12_ensure_element_data_for_adapter_luid:
 * @element: a #GstElement
 * @context: a #GstContext
 * @adapter_luid: an int64 representation of DXGI adapter LUID
 * @device: (inout) (transfer full): location of a #GstD3D12Device
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * D3D12 capable elements.
 *
 * Retrieve's the #GstD3D12Device in @context and places the result in @device.
 * @device is accepted only when @adapter_index is equal to that of @device
 *
 * Returns: whether the @device could be set successfully
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_ensure_element_data_for_adapter_luid (GstElement * element,
    gint64 adapter_luid, GstD3D12Device ** device)
{
  std::lock_guard < std::recursive_mutex > lk (context_lock_);

  g_return_val_if_fail (element != nullptr, FALSE);
  g_return_val_if_fail (device != nullptr, FALSE);

  init_context_debug ();

  if (*device) {
    GST_LOG_OBJECT (element, "already have a device %" GST_PTR_FORMAT, *device);
    return TRUE;
  }

  run_d3d12_context_query (element, device);
  if (*device)
    return TRUE;

  *device = gst_d3d12_device_new_for_adapter_luid (adapter_luid);

  if (*device == nullptr) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with adapter luid %" G_GINT64_FORMAT,
        adapter_luid);
    return FALSE;
  } else {
    GstContext *context;
    GstMessage *msg;

    /* Propagate new D3D12 device context */
    context = gst_context_new (GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);
    context_set_d3d12_device (context, *device);

    gst_element_set_context (element, context);

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting have context (%p) message with D3D12 device context (%p)",
        context, *device);
    msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

/**
 * gst_d3d12_luid_to_int64:
 * @luid: A pointer to LUID struct
 *
 * Converts @luid to a 64-bit signed integer.
 * See also Int64FromLuid method defined in
 * windows.devices.display.core.interop.h Windows SDK header
 *
 * Since: 1.26
 */
gint64
gst_d3d12_luid_to_int64 (const LUID * luid)
{
  LARGE_INTEGER val;

  g_return_val_if_fail (luid != nullptr, 0);

  val.LowPart = luid->LowPart;
  val.HighPart = luid->HighPart;

  return val.QuadPart;
}

/**
 * gst_d3d12_context_new:
 * @device: (transfer none): a #GstD3D12Device
 *
 * Creates a new #GstContext object with @device
 *
 * Returns: a #GstContext object
 *
 * Since: 1.26
 */
GstContext *
gst_d3d12_context_new (GstD3D12Device * device)
{
  GstContext *context;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  context = gst_context_new (GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);
  context_set_d3d12_device (context, device);

  return context;
}

/**
 * gst_d3d12_create_user_token:
 *
 * Creates new user token value
 *
 * Returns: user token value
 *
 * Since: 1.26
 */
gint64
gst_d3d12_create_user_token (void)
{
  /* *INDENT-OFF* */
  static std::atomic<gint64> user_token { 0 };
  /* *INDENT-ON* */

  return user_token.fetch_add (1);
}

static gboolean
gst_d3d12_buffer_copy_into_fallback (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, (GstVideoInfo *) info, src,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR ("Couldn't map src frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, (GstVideoInfo *) info, dst,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR ("Couldn't map dst frame");
    gst_video_frame_unmap (&in_frame);
    return FALSE;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;
}

static GstD3D12Device *
get_device_from_buffer (GstBuffer * buffer)
{
  GstD3D12Device *device = nullptr;
  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    if (!gst_is_d3d12_memory (mem))
      return nullptr;

    auto dmem = GST_D3D12_MEMORY_CAST (mem);
    if (!device)
      device = dmem->device;
    else if (!gst_d3d12_device_is_equal (device, dmem->device))
      return nullptr;
  }

  return device;
}

/**
 * gst_d3d12_buffer_copy_into:
 * @dest: a #GstBuffer
 * @src: a #GstBuffer
 * @info: a #GstVideoInfo
 *
 * Copy @src data into @dest. This method executes only memory copy.
 * Use gst_buffer_copy_into() method for metadata copy
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_buffer_copy_into (GstBuffer * dest, GstBuffer * src,
    const GstVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_BUFFER (dest), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src), FALSE);
  g_return_val_if_fail (info, FALSE);

  auto num_mem = gst_buffer_n_memory (dest);
  if (gst_buffer_n_memory (src) != num_mem)
    return gst_d3d12_buffer_copy_into_fallback (dest, src, info);

  auto dest_device = get_device_from_buffer (dest);
  auto src_device = get_device_from_buffer (src);

  if (!dest_device || !src_device ||
      !gst_d3d12_device_is_equal (dest_device, src_device)) {
    return gst_d3d12_buffer_copy_into_fallback (dest, src, info);
  }

  GstD3D12Frame src_frame, dest_frame;
  if (!gst_d3d12_frame_map (&src_frame, info, src, GST_MAP_READ_D3D12,
          GST_D3D12_FRAME_MAP_FLAG_NONE)) {
    GST_ERROR ("Couldn't map src buffer");
    return FALSE;
  }

  if (!gst_d3d12_frame_map (&dest_frame, info, dest, GST_MAP_WRITE_D3D12,
          GST_D3D12_FRAME_MAP_FLAG_NONE)) {
    GST_ERROR ("Couldn't map dest buffer");
    gst_d3d12_frame_unmap (&src_frame);
    return FALSE;
  }

  guint64 fence_val = 0;
  auto ret = gst_d3d12_frame_copy (&dest_frame, &src_frame, &fence_val);
  gst_d3d12_frame_unmap (&dest_frame);
  gst_d3d12_frame_unmap (&src_frame);

  if (ret) {
    auto fence = gst_d3d12_device_get_fence_handle (dest_device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    gst_d3d12_buffer_set_fence (dest, fence, fence_val, FALSE);
  }

  return ret;
}

/**
 * gst_d3d12_buffer_set_fence:
 * @buffer: a #GstBuffer
 * @fence: (allow-none): a ID3D12Fence
 * @fence_value: fence value
 * @wait: waits previously configured fence in buffer
 *
 * Should be called after GPU write operation against @buffer.
 * This method will call gst_d3d12_memory_set_fence() for each memory in @buffer
 * and sets #GstD3D12MemoryTransfer flags to memory objects
 *
 * Since: 1.26
 */
void
gst_d3d12_buffer_set_fence (GstBuffer * buffer, ID3D12Fence * fence,
    guint64 fence_value, gboolean wait)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));

  auto num_mem = gst_buffer_n_memory (buffer);
  for (guint i = 0; i < num_mem; i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    if (!gst_is_d3d12_memory (mem))
      return;

    gst_d3d12_memory_set_fence (GST_D3D12_MEMORY_CAST (mem),
        fence, fence_value, wait);
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
    GST_MINI_OBJECT_FLAG_UNSET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);
  }
}

/**
 * _gst_d3d12_result:
 * @result: HRESULT D3D12 API return code
 * @device: (nullable): Associated #GstD3D12Device
 * @cat: a #GstDebugCategory
 * @file: the file that checking the result code
 * @function: the function that checking the result code
 * @line: the line that checking the result code
 * @level: #GstDebugLevel
 *
 * Prints debug message if @result code indicates the operation was failed.
 *
 * Returns: %TRUE if D3D12 API call result is SUCCESS
 *
 * Since: 1.26
 */
gboolean
_gst_d3d12_result (HRESULT hr, GstD3D12Device * device, GstDebugCategory * cat,
    const gchar * file, const gchar * function, gint line, GstDebugLevel level)
{
#ifndef GST_DISABLE_GST_DEBUG
  if (device)
    gst_d3d12_device_d3d12_debug (device, file, function, line);

  if (FAILED (hr)) {
    gchar *error_text = nullptr;
    error_text = g_win32_error_message ((guint) hr);
    /* g_win32_error_message() doesn't cover all HERESULT return code,
     * so it could be empty string, or nullptr if there was an error
     * in g_utf16_to_utf8() */
    gst_debug_log (cat, level, file, function, line,
        nullptr, "D3D12 call failed: 0x%x, %s", (guint) hr,
        GST_STR_NULL (error_text));
    g_free (error_text);
  }
#endif

  if (SUCCEEDED (hr))
    return TRUE;

  if (device)
    gst_d3d12_device_check_device_removed (device);

  return FALSE;
}
