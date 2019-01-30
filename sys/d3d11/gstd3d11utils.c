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

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_utils_debug);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

static GstDebugCategory *
_init_d3d11_utils_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_d3d11_utils_debug, "d3d11utils", 0,
        "Direct3D11 Utilities");
    g_once_init_leave (&_init, 1);
  }

  return gst_d3d11_utils_debug;
}

static void
_init_context_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
}

#define GST_CAT_DEFAULT _init_d3d11_utils_debug()

static const struct
{
  GstVideoFormat gst_format;
  DXGI_FORMAT dxgi_format;
} gst_dxgi_format_map[] = {
  /* TODO: add more formats */
  {
  GST_VIDEO_FORMAT_BGRA, DXGI_FORMAT_B8G8R8A8_UNORM}, {
  GST_VIDEO_FORMAT_RGBA, DXGI_FORMAT_R8G8B8A8_UNORM}, {
  GST_VIDEO_FORMAT_RGB10A2_LE, DXGI_FORMAT_R10G10B10A2_UNORM}
};

GstVideoFormat
gst_d3d11_dxgi_format_to_gst (DXGI_FORMAT format)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (gst_dxgi_format_map); i++) {
    if (gst_dxgi_format_map[i].dxgi_format == format)
      return gst_dxgi_format_map[i].gst_format;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

DXGI_FORMAT
gst_d3d11_dxgi_format_from_gst (GstVideoFormat format)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (gst_dxgi_format_map); i++) {
    if (gst_dxgi_format_map[i].gst_format == format)
      return gst_dxgi_format_map[i].dxgi_format;
  }

  return DXGI_FORMAT_UNKNOWN;
}

typedef struct
{
  GstCaps *caps;
  D3D11_FORMAT_SUPPORT flags;
} SupportCapsData;

static void
gst_d3d11_device_get_supported_caps_internal (GstD3D11Device * device,
    SupportCapsData * data)
{
  ID3D11Device *d3d11_device;
  HRESULT hr;
  gint i;
  GValue v_list = G_VALUE_INIT;
  GstCaps *supported_caps;

  d3d11_device = gst_d3d11_device_get_device (device);
  g_value_init (&v_list, GST_TYPE_LIST);

  for (i = 0; i < G_N_ELEMENTS (gst_dxgi_format_map); i++) {
    UINT format_support = 0;
    GstVideoFormat format = gst_dxgi_format_map[i].gst_format;

    hr = ID3D11Device_CheckFormatSupport (d3d11_device,
        gst_dxgi_format_map[i].dxgi_format, &format_support);

    if (SUCCEEDED (hr) && ((format_support & data->flags) == data->flags)) {
      GValue v_str = G_VALUE_INIT;
      g_value_init (&v_str, G_TYPE_STRING);

      GST_LOG_OBJECT (device, "d3d11 device can support %s with flags 0x%x",
          gst_video_format_to_string (format), data->flags);
      g_value_set_string (&v_str, gst_video_format_to_string (format));
      gst_value_list_append_and_take_value (&v_list, &v_str);
    }
  }

  supported_caps = gst_caps_new_simple ("video/x-raw",
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  gst_caps_set_value (supported_caps, "format", &v_list);
  g_value_unset (&v_list);

  data->caps = supported_caps;
}

/**
 * gst_d3d11_device_get_supported_caps:
 * @device: a #GstD3DDevice
 * @flags: D3D11_FORMAT_SUPPORT flags
 *
 * Check supported format with given flags
 *
 * Returns: a #GstCaps representing supported format
 */
GstCaps *
gst_d3d11_device_get_supported_caps (GstD3D11Device * device,
    D3D11_FORMAT_SUPPORT flags)
{
  SupportCapsData data;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  data.caps = NULL;
  data.flags = flags;

  gst_d3d11_device_thread_add (device, (GstD3D11DeviceThreadFunc)
      gst_d3d11_device_get_supported_caps_internal, &data);

  return data.caps;
}

gboolean
gst_d3d11_calculate_buffer_size (GstVideoInfo * info, guint pitch,
    gsize offset[GST_VIDEO_MAX_PLANES], gint stride[GST_VIDEO_MAX_PLANES],
    gsize * size)
{
  g_return_val_if_fail (info != NULL, FALSE);

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      offset[0] = 0;
      stride[0] = pitch;
      *size = pitch * GST_VIDEO_INFO_HEIGHT (info);
      break;
    case GST_VIDEO_FORMAT_NV12:
      offset[0] = 0;
      stride[0] = pitch;
      offset[1] = offset[0] + stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (info, 0);
      stride[1] = pitch;
      *size = offset[1] + stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (info, 1);
      break;
    default:
      return FALSE;
  }

  GST_LOG ("Calculated buffer size: %" G_GSIZE_FORMAT
      " (%s %dx%d, Pitch %d)", *size,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), pitch);

  return TRUE;
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
    GstD3D11Device ** device)
{
  GstD3D11Device *device_replacement = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  if (!context)
    return FALSE;

  if (!gst_context_get_d3d11_device (context, &device_replacement))
    return FALSE;

  if (*device)
    gst_object_unref (*device);
  *device = device_replacement;

  return TRUE;
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

  gst_context_set_d3d11_device (context, device);
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
run_d3d11_context_query (GstElement * element)
{
  GstQuery *query;
  GstContext *ctxt;

  /*  2a) Query downstream with GST_QUERY_CONTEXT for the context and
   *      check if downstream already has a context of the specific type
   *  2b) Query upstream as above.
   */
  query = gst_query_new_context (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE);
  if (run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%" GST_PTR_FORMAT ") in downstream query", ctxt);
    gst_element_set_context (element, ctxt);
  } else if (run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%" GST_PTR_FORMAT ") in upstream query", ctxt);
    gst_element_set_context (element, ctxt);
  } else {
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
 * @device: (inout): the resulting #GstD3D11Device
 * @preferred_adapter: the index of preferred adapter
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
gst_d3d11_ensure_element_data (GstElement * element, GstD3D11Device ** device,
    gint preferred_adapter)
{
  GstD3D11Device *new_device;
  GstContext *context;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (device != NULL, FALSE);
  _init_context_debug ();

  if (*device) {
    GST_LOG_OBJECT (element, "already have a device %" GST_PTR_FORMAT, *device);
    return TRUE;
  }

  run_d3d11_context_query (element);

  /* Neighbour found and it updated the devicey */
  if (*device) {
    return TRUE;
  }

  new_device = gst_d3d11_device_new (preferred_adapter);

  if (!new_device) {
    GST_ERROR_OBJECT (element,
        "Couldn't create new device with adapter index %d", preferred_adapter);
    return FALSE;
  }

  *device = new_device;

  context = gst_context_new (GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE, TRUE);
  gst_context_set_d3d11_device (context, new_device);

  gst_element_set_context (element, context);

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting have context (%" GST_PTR_FORMAT
      ") message with device (%" GST_PTR_FORMAT ")", context, device);

  gst_element_post_message (GST_ELEMENT_CAST (element),
      gst_message_new_have_context (GST_OBJECT_CAST (element), context));

  return TRUE;
}
