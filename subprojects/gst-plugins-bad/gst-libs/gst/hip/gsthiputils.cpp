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
