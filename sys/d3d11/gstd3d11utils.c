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

#include "gstd3d11utils.h"
#include "gstd3d11device.h"

#include <windows.h>
#include <versionhelpers.h>

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_utils_debug);
#define GST_CAT_DEFAULT gst_d3d11_utils_debug

static void
_init_context_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_d3d11_handle_set_context:
 * @element: a #GstElement
 * @context: a #GstContext
 * @device: (inout) (transfer full): location of a #GstD3DDevice
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * D3D11 capable elements.
 *
 * Retrieve's the #GstD3D11Device in @context and places the result in @device.
 *
 * Returns: whether the @device could be set successfully
 */
gboolean
gst_d3d11_handle_set_context (GstElement * element, GstContext * context,
    gint adapter, GstD3D11Device ** device)
{
  const gchar *context_type;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  _init_context_debug ();

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE) == 0) {
    const GstStructure *str;
    GstD3D11Device *other_device = NULL;
    guint other_adapter = 0;

    /* If we had device already, will not replace it */
    if (*device)
      return TRUE;

    str = gst_context_get_structure (context);

    if (gst_structure_get (str, "device", GST_TYPE_D3D11_DEVICE,
            &other_device, "adapter", G_TYPE_UINT, &other_adapter, NULL)) {
      if (adapter == -1 || (guint) adapter == other_adapter) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_CONTEXT,
            element, "Found D3D11 device context");
        *device = other_device;

        return TRUE;
      }

      gst_object_unref (other_device);
    }
  }

  return FALSE;
}

static void
context_set_d3d11_device (GstContext * context, GstD3D11Device * device)
{
  GstStructure *s;
  guint adapter = 0;
  guint device_id = 0;
  guint vendor_id = 0;
  gboolean hardware = FALSE;
  gchar *desc = NULL;

  g_return_if_fail (context != NULL);

  g_object_get (G_OBJECT (device), "adapter", &adapter, "device-id", &device_id,
      "vendor_id", &vendor_id, "hardware", &hardware, "description", &desc,
      NULL);

  GST_CAT_LOG (GST_CAT_CONTEXT,
      "setting GstD3D11Device(%" GST_PTR_FORMAT
      ") with adapter %d on context(%" GST_PTR_FORMAT ")",
      device, adapter, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, "device", GST_TYPE_D3D11_DEVICE, device,
      "adapter", G_TYPE_UINT, adapter,
      "device-id", G_TYPE_UINT, device_id,
      "vendor-id", G_TYPE_UINT, vendor_id,
      "hardware", G_TYPE_BOOLEAN, hardware,
      "description", G_TYPE_STRING, GST_STR_NULL (desc), NULL);
  g_free (desc);
}

/**
 * gst_d3d11_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type %GST_QUERY_CONTEXT
 * @device: (transfer none) (nullable): a #GstD3D11Device
 *
 * Returns: Whether the @query was successfully responded to from the passed
 *          @device.
 */
gboolean
gst_d3d11_handle_context_query (GstElement * element, GstQuery * query,
    GstD3D11Device * device)
{
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  _init_context_debug ();

  GST_LOG_OBJECT (element, "handle context query %" GST_PTR_FORMAT, query);

  if (!device)
    return FALSE;

  gst_query_parse_context_type (query, &context_type);
  if (g_strcmp0 (context_type, GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE) != 0)
    return FALSE;

  gst_query_parse_context (query, &old_context);
  if (old_context)
    context = gst_context_copy (old_context);
  else
    context = gst_context_new (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);

  context_set_d3d11_device (context, device);
  gst_query_set_context (query, context);
  gst_context_unref (context);

  GST_DEBUG_OBJECT (element, "successfully set %" GST_PTR_FORMAT
      " on %" GST_PTR_FORMAT, device, query);

  return TRUE;
}

static gboolean
pad_query (const GValue * item, GValue * value, gpointer user_data)
{
  GstPad *pad = g_value_get_object (item);
  GstQuery *query = user_data;
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
  GValue res = { 0 };

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
run_d3d11_context_query (GstElement * element, GstD3D11Device ** device)
{
  GstQuery *query;
  GstContext *ctxt = NULL;

  /* 1) Query downstream with GST_QUERY_CONTEXT for the context and
   *    check if downstream already has a context of the specific type
   */
  query = gst_query_new_context (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE);
  if (run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    if (ctxt) {
      GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
          "found context (%" GST_PTR_FORMAT ") in downstream query", ctxt);
      gst_element_set_context (element, ctxt);
    }
  }

  /* 2) although we found d3d11 device context above, the element does not want
   *    to use the context. Then try to find from the other direction */
  if (*device == NULL && run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    if (ctxt) {
      GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
          "found context (%" GST_PTR_FORMAT ") in upstream query", ctxt);
      gst_element_set_context (element, ctxt);
    }
  }

  if (*device == NULL) {
    /* 3) Post a GST_MESSAGE_NEED_CONTEXT message on the bus with
     *    the required context type and afterwards check if a
     *    usable context was set now as in 1). The message could
     *    be handled by the parent bins of the element and the
     *    application.
     */
    GstMessage *msg;

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting need context message");
    msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE);
    gst_element_post_message (element, msg);
  }

  /*
   * Whomever responds to the need-context message performs a
   * GstElement::set_context() with the required context in which the element
   * is required to update the display_ptr or call gst_gl_handle_set_context().
   */

  gst_query_unref (query);
}

/**
 * gst_d3d11_ensure_element_data:
 * @element: the #GstElement running the query
 * @adapter: prefered adapter index, pass adapter >=0 when
 *           the adapter explicitly required. Otherwise, set -1.
 * @device: (inout): the resulting #GstD3D11Device
 *
 * Perform the steps necessary for retrieving a #GstD3D11Device
 * from the surrounding elements or from the application using the #GstContext mechanism.
 *
 * If the contents of @device is not %NULL, then no #GstContext query is
 * necessary for #GstD3D11Device retrieval is performed.
 *
 * Returns: whether a #GstD3D11Device exists in @device
 */
gboolean
gst_d3d11_ensure_element_data (GstElement * element, gint adapter,
    GstD3D11Device ** device)
{
  guint target_adapter = 0;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  _init_context_debug ();

  if (*device) {
    GST_LOG_OBJECT (element, "already have a device %" GST_PTR_FORMAT, *device);
    return TRUE;
  }

  run_d3d11_context_query (element, device);
  if (*device)
    return TRUE;

  if (adapter > 0)
    target_adapter = adapter;

  *device = gst_d3d11_device_new (target_adapter);

  if (*device == NULL) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with adapter index %d", target_adapter);
    return FALSE;
  } else {
    GstContext *context;
    GstMessage *msg;

    /* Propagate new D3D11 device context */

    context = gst_context_new (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);
    context_set_d3d11_device (context, *device);

    gst_element_set_context (element, context);

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting have context (%p) message with D3D11 device context (%p)",
        context, *device);
    msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

gboolean
gst_d3d11_is_windows_8_or_greater (void)
{
  static gsize version_once = 0;
  static gboolean ret = FALSE;

  if (g_once_init_enter (&version_once)) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    if (IsWindows8OrGreater ())
      ret = TRUE;
#else
    ret = TRUE;
#endif

    g_once_init_leave (&version_once, 1);
  }

  return ret;
}

GstD3D11DeviceVendor
gst_d3d11_get_device_vendor (GstD3D11Device * device)
{
  guint device_id = 0;
  guint vendor_id = 0;
  gchar *desc = NULL;
  GstD3D11DeviceVendor vendor = GST_D3D11_DEVICE_VENDOR_UNKNOWN;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);

  g_object_get (device, "device-id", &device_id, "vendor-id", &vendor_id,
      "description", &desc, NULL);

  switch (vendor_id) {
    case 0:
      if (device_id == 0 && desc && g_strrstr (desc, "SraKmd"))
        vendor = GST_D3D11_DEVICE_VENDOR_XBOX;
      break;
    case 0x1002:
    case 0x1022:
      vendor = GST_D3D11_DEVICE_VENDOR_AMD;
      break;
    case 0x8086:
      vendor = GST_D3D11_DEVICE_VENDOR_INTEL;
      break;
    case 0x10de:
      vendor = GST_D3D11_DEVICE_VENDOR_NVIDIA;
      break;
    case 0x4d4f4351:
      vendor = GST_D3D11_DEVICE_VENDOR_QUALCOMM;
      break;
    default:
      break;
  }

  g_free (desc);

  return vendor;
}

gboolean
_gst_d3d11_result (HRESULT hr, GstD3D11Device * device, GstDebugCategory * cat,
    const gchar * file, const gchar * function, gint line)
{
#ifndef GST_DISABLE_GST_DEBUG
  gboolean ret = TRUE;

  if (FAILED (hr)) {
    gchar *error_text = NULL;

    error_text = g_win32_error_message ((guint) hr);
    /* g_win32_error_message() doesn't cover all HERESULT return code,
     * so it could be empty string, or null if there was an error
     * in g_utf16_to_utf8() */
    gst_debug_log (cat, GST_LEVEL_WARNING, file, function, line,
        NULL, "D3D11 call failed: 0x%x, %s", (guint) hr,
        GST_STR_NULL (error_text));
    g_free (error_text);

    ret = FALSE;
  }
#if (HAVE_D3D11SDKLAYERS_H || HAVE_DXGIDEBUG_H)
  if (device) {
    gst_d3d11_device_d3d11_debug (device, file, function, line);
    gst_d3d11_device_dxgi_debug (device, file, function, line);
  }
#endif

  return ret;
#else
  return SUCCEEDED (hr);
#endif
}
