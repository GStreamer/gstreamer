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

gboolean
_gst_hip_result (hipError_t result, GstDebugCategory * cat, const gchar * file,
    const gchar * function, gint line)
{
  if (result != hipSuccess) {
#ifndef GST_DISABLE_GST_DEBUG
    auto error_name = hipGetErrorName (result);
    auto error_str = hipGetErrorString (result);
    gst_debug_log (cat, GST_LEVEL_ERROR, file, function, line,
        NULL, "HIP call failed: %s, %s", error_name, error_str);
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
  g_object_get (device, "device-id", &device_id, nullptr);

  auto s = gst_context_writable_structure (context);
  gst_structure_set (s, "device", GST_TYPE_HIP_DEVICE, device,
      "device-id", G_TYPE_UINT, device_id, nullptr);
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

gboolean
gst_hip_ensure_element_data (GstElement * element, gint device_id,
    GstHipDevice ** device)
{
  if (*device)
    return TRUE;

  run_hip_context_query (element, device);
  if (*device)
    return TRUE;

  guint target_device_id = 0;
  if (device_id > 0)
    target_device_id = device_id;

  *device = gst_hip_device_new (target_device_id);

  if (*device == nullptr) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with adapter index %d", target_device_id);
    return FALSE;
  } else {
    auto ctx = gst_context_new_hip_device (*device);
    gst_element_set_context (element, ctx);
    auto msg = gst_message_new_have_context (GST_OBJECT_CAST (element), ctx);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

gboolean
gst_hip_handle_set_context (GstElement * element, GstContext * context,
    gint device_id, GstHipDevice ** device)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != nullptr, FALSE);

  if (!context)
    return FALSE;

  auto context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_HIP_DEVICE_CONTEXT_TYPE) == 0) {
    GstHipDevice *other_device = nullptr;
    guint other_idx = 0;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    auto s = gst_context_get_structure (context);
    if (gst_structure_get (s, "device", GST_TYPE_HIP_DEVICE, &other_device,
            "device-id", G_TYPE_UINT, &other_idx, nullptr)) {
      if (device_id == -1 || (guint) device_id == other_idx) {
        *device = other_device;
        return TRUE;
      }

      gst_object_unref (other_device);
    }
  }

  return FALSE;
}

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

GstContext *
gst_context_new_hip_device (GstHipDevice * device)
{
  g_return_val_if_fail (GST_HIP_DEVICE (device), nullptr);

  auto ctx = gst_context_new (GST_HIP_DEVICE_CONTEXT_TYPE, TRUE);
  context_set_hip_device (ctx, device);

  return ctx;
}
