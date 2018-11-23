/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#include "gstcudautils.h"
#include "gstcudacontext.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_utils_debug);
#define GST_CAT_DEFAULT gst_cuda_utils_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

static void
_init_debug (void)
{
  static volatile gsize once_init = 0;

  if (g_once_init_enter (&once_init)) {

    GST_DEBUG_CATEGORY_INIT (gst_cuda_utils_debug, "cudautils", 0,
        "CUDA utils");
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&once_init, 1);
  }
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
find_cuda_context (GstElement * element, GstCudaContext ** cuda_ctx)
{
  GstQuery *query;
  GstContext *ctxt;

  /*  1) Query downstream with GST_QUERY_CONTEXT for the context and
   *      check if upstream already has a context of the specific type
   *  2) Query upstream as above.
   */
  query = gst_query_new_context (GST_CUDA_CONTEXT_TYPE);
  if (run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in downstream query", ctxt);
    gst_element_set_context (element, ctxt);
  }

  /* although we found cuda context above, the element does not want
   * to use the context. Then try to find from the other direction */
  if (*cuda_ctx == NULL && run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in upstream query", ctxt);
    gst_element_set_context (element, ctxt);
  }

  if (*cuda_ctx == NULL) {
    /* 3) Post a GST_MESSAGE_NEED_CONTEXT message on the bus with
     *    the required context type and afterwards check if a
     *    usable context was set now. The message could
     *    be handled by the parent bins of the element and the
     *    application.
     */
    GstMessage *msg;

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting need context message");
    msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        GST_CUDA_CONTEXT_TYPE);
    gst_element_post_message (element, msg);
  }

  /*
   * Whomever responds to the need-context message performs a
   * GstElement::set_context() with the required context in which the element
   * is required to update the cuda_ctx or call gst_cuda_handle_set_context().
   */

  gst_query_unref (query);
}

static void
context_set_cuda_context (GstContext * context, GstCudaContext * cuda_ctx)
{
  GstStructure *s;
  gint device_id;

  g_return_if_fail (context != NULL);

  g_object_get (G_OBJECT (cuda_ctx), "cuda-device-id", &device_id, NULL);

  GST_CAT_LOG (GST_CAT_CONTEXT,
      "setting GstCudaContext(%" GST_PTR_FORMAT
      ") with cuda-device-id %d on context(%" GST_PTR_FORMAT ")",
      cuda_ctx, device_id, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, GST_CUDA_CONTEXT_TYPE, GST_TYPE_CUDA_CONTEXT,
      cuda_ctx, "cuda-device-id", G_TYPE_INT, device_id, NULL);
}

/**
 * gst_cuda_ensure_element_context:
 * @element: the #GstElement running the query
 * @device_id: prefered device-id, pass device_id >=0 when
 *             the device_id explicitly required. Otherwise, set -1.
 * @cuda_ctx: (inout): the resulting #GstCudaContext
 *
 * Perform the steps necessary for retrieving a #GstCudaContext from the
 * surrounding elements or from the application using the #GstContext mechanism.
 *
 * If the content of @cuda_ctx is not %NULL, then no #GstContext query is
 * necessary for #GstCudaContext.
 *
 * Returns: whether a #GstCudaContext exists in @cuda_ctx
 */
gboolean
gst_cuda_ensure_element_context (GstElement * element, gint device_id,
    GstCudaContext ** cuda_ctx)
{
  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (cuda_ctx != NULL, FALSE);

  _init_debug ();

  if (*cuda_ctx)
    return TRUE;

  find_cuda_context (element, cuda_ctx);
  if (*cuda_ctx)
    return TRUE;

  /* No available CUDA context in pipeline, create new one here */
  *cuda_ctx = gst_cuda_context_new (device_id);

  if (*cuda_ctx == NULL) {
    GST_CAT_ERROR_OBJECT (GST_CAT_CONTEXT, element,
        "Failed to create CUDA context with device-id %d", device_id);
    return FALSE;
  } else {
    GstContext *context;
    GstMessage *msg;

    /* Propagate new CUDA context */

    context = gst_context_new (GST_CUDA_CONTEXT_TYPE, TRUE);
    context_set_cuda_context (context, *cuda_ctx);

    gst_element_set_context (element, context);

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting have context (%p) message with CUDA context (%p)",
        context, *cuda_ctx);
    msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}

/**
 * gst_cuda_handle_set_context:
 * @element: a #GstElement
 * @context: a #GstContext
 * @device_id: prefered device-id, pass device_id >=0 when
 *             the device_id explicitly required. Otherwise, set -1.
 * @cuda_ctx: (inout) (transfer full): location of a #GstCudaContext
 *
 * Helper function for implementing #GstElementClass.set_context() in
 * CUDA capable elements.
 *
 * Retrieves the #GstCudaContext in @context and places the result in @cuda_ctx.
 *
 * Returns: whether the @cuda_ctx could be set successfully
 */
gboolean
gst_cuda_handle_set_context (GstElement * element,
    GstContext * context, gint device_id, GstCudaContext ** cuda_ctx)
{
  const gchar *context_type;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (cuda_ctx != NULL, FALSE);

  _init_debug ();

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_CUDA_CONTEXT_TYPE) == 0) {
    const GstStructure *str;
    GstCudaContext *other_ctx = NULL;
    gint other_device_id = 0;

    /* If we had context already, will not replace it */
    if (*cuda_ctx)
      return TRUE;

    str = gst_context_get_structure (context);
    if (gst_structure_get (str, GST_CUDA_CONTEXT_TYPE, GST_TYPE_CUDA_CONTEXT,
            &other_ctx, NULL)) {
      g_object_get (other_ctx, "cuda-device-id", &other_device_id, NULL);

      if (device_id == -1 || other_device_id == device_id) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_CONTEXT, element, "Found CUDA context");
        *cuda_ctx = other_ctx;

        return TRUE;
      }

      gst_object_unref (other_ctx);
    }
  }

  return FALSE;
}

/**
 * gst_cuda_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type %GST_QUERY_CONTEXT
 * @cuda_ctx: (transfer none) (nullable): a #GstCudaContext
 *
 * Returns: Whether the @query was successfully responded to from the passed
 *          @context.
 */
gboolean
gst_cuda_handle_context_query (GstElement * element,
    GstQuery * query, GstCudaContext * cuda_ctx)
{
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);
  g_return_val_if_fail (cuda_ctx == NULL
      || GST_IS_CUDA_CONTEXT (cuda_ctx), FALSE);

  _init_debug ();

  GST_CAT_LOG_OBJECT (GST_CAT_CONTEXT, element,
      "handle context query %" GST_PTR_FORMAT, query);
  gst_query_parse_context_type (query, &context_type);

  if (cuda_ctx && g_strcmp0 (context_type, GST_CUDA_CONTEXT_TYPE) == 0) {
    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_CUDA_CONTEXT_TYPE, TRUE);

    context_set_cuda_context (context, cuda_ctx);
    gst_query_set_context (query, context);
    gst_context_unref (context);
    GST_CAT_DEBUG_OBJECT (GST_CAT_CONTEXT, element,
        "successfully set %" GST_PTR_FORMAT " on %" GST_PTR_FORMAT, cuda_ctx,
        query);

    return TRUE;
  }

  return FALSE;
}

/**
 * gst_context_new_cuda_context:
 * @cuda_ctx: (transfer none) a #GstCudaContext
 *
 * Returns: (transfer full) (nullable): a new #GstContext embedding the @cuda_ctx
 * or %NULL
 */
GstContext *
gst_context_new_cuda_context (GstCudaContext * cuda_ctx)
{
  GstContext *context;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (cuda_ctx), NULL);

  context = gst_context_new (GST_CUDA_CONTEXT_TYPE, TRUE);
  context_set_cuda_context (context, cuda_ctx);

  return context;
}
