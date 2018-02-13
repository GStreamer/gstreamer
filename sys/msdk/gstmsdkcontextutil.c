/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * Copyright (c) 2018, Igalia S.L.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gstmsdkcontextutil.h"

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

static void
_init_context_debug (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
#endif
}

static gboolean
context_pad_query (const GValue * item, GValue * value, gpointer user_data)
{
  GstPad *const pad = g_value_get_object (item);
  GstQuery *const query = user_data;

  if (gst_pad_peer_query (pad, query)) {
    g_value_set_boolean (value, TRUE);
    return FALSE;
  }

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, pad, "context pad peer query failed");
  return TRUE;
}

static gboolean
_gst_context_run_query (GstElement * element, GstQuery * query,
    GstPadDirection direction)
{
  GstIteratorFoldFunction const func = context_pad_query;
  GstIterator *it;
  GValue res = { 0 };

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

  /* Ask neighbour */
  if (direction == GST_PAD_SRC)
    it = gst_element_iterate_src_pads (element);
  else
    it = gst_element_iterate_sink_pads (element);

  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);
  gst_iterator_free (it);

  return g_value_get_boolean (&res);
}

static gboolean
_gst_context_get_from_query (GstElement * element, GstQuery * query,
    GstPadDirection direction)
{
  GstContext *ctxt;

  if (!_gst_context_run_query (element, query, direction))
    return FALSE;

  gst_query_parse_context (query, &ctxt);
  if (!ctxt)
    return FALSE;

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "found context (%" GST_PTR_FORMAT ") in %s query", ctxt,
      direction == GST_PAD_SRC ? "downstream" : "upstream");

  gst_element_set_context (element, ctxt);
  return TRUE;
}

static void
_gst_context_query (GstElement * element, const gchar * context_type)
{
  GstQuery *query;
  GstMessage *msg;

  /* 2) Query downstream with GST_QUERY_CONTEXT for the context and
     check if downstream already has a context of the specific
     type */

  /* 3) Query upstream with GST_QUERY_CONTEXT for the context and
     check if upstream already has a context of the specific
     type */
  query = gst_query_new_context (context_type);
  if (_gst_context_get_from_query (element, query, GST_PAD_SRC))
    goto found;
  if (_gst_context_get_from_query (element, query, GST_PAD_SINK))
    goto found;

  /* 4) Post a GST_MESSAGE_NEED_CONTEXT message on the bus with
     the required context types and afterwards check if an
     usable context was set now as in 1). The message could
     be handled by the parent bins of the element and the
     application. */
  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting `need-context' message");

  msg = gst_message_new_need_context (GST_OBJECT_CAST (element), context_type);
  if (!gst_element_post_message (element, msg))
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element, "No bus attached");

  /* Whomever responds to the need-context message performs a
     GstElement::set_context() with the required context in which the
     element is required to update the display_ptr */

found:
  gst_query_unref (query);
}

gboolean
gst_msdk_context_prepare (GstElement * element, GstMsdkContext ** context_ptr)
{
  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (context_ptr != NULL, FALSE);

  _init_context_debug ();

  /* 1) Check if the element already has a context of the specific type. */
  if (*context_ptr) {
    GST_LOG_OBJECT (element, "already have a context %" GST_PTR_FORMAT,
        *context_ptr);
    return TRUE;
  }

  _gst_context_query (element, GST_MSDK_CONTEXT_TYPE_NAME);

  if (*context_ptr)
    GST_LOG_OBJECT (element, "found a context %" GST_PTR_FORMAT, *context_ptr);

  return *context_ptr != NULL;
}

gboolean
gst_msdk_context_get_context (GstContext * context,
    GstMsdkContext ** msdk_context)
{
  const GstStructure *structure;
  const gchar *type;

  g_return_val_if_fail (GST_IS_CONTEXT (context), FALSE);

  type = gst_context_get_context_type (context);

  if (!g_strcmp0 (type, GST_MSDK_CONTEXT_TYPE_NAME)) {
    structure = gst_context_get_structure (context);
    return gst_structure_get (structure, GST_MSDK_CONTEXT_TYPE_NAME,
        GST_TYPE_MSDK_CONTEXT, msdk_context, NULL);
  }

  return FALSE;
}

static void
gst_msdk_context_propagate (GstElement * element, GstMsdkContext * msdk_context)
{
  GstContext *context;
  GstStructure *structure;
  GstMessage *msg;

  context = gst_context_new (GST_MSDK_CONTEXT_TYPE_NAME, FALSE);

  structure = gst_context_writable_structure (context);
  gst_structure_set (structure, GST_MSDK_CONTEXT_TYPE_NAME,
      GST_TYPE_MSDK_CONTEXT, msdk_context, NULL);

  gst_element_set_context (element, context);

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting `have-context' message with MSDK context %" GST_PTR_FORMAT,
      msdk_context);

  msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
  if (!gst_element_post_message (element, msg)) {
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element, "No bus attached");
  }
}

gboolean
gst_msdk_context_ensure_context (GstElement * element, gboolean hardware,
    GstMsdkContextJobType job)
{
  GstMsdkContext *msdk_context;

  msdk_context = gst_msdk_context_new (hardware, job);
  if (!msdk_context) {
    GST_ERROR_OBJECT (element, "Context creation failed");
    return FALSE;
  }

  GST_INFO_OBJECT (element, "New MSDK Context %p", msdk_context);

  gst_msdk_context_propagate (element, msdk_context);
  gst_object_unref (msdk_context);

  return TRUE;
}
