/*
 *  gstvaapivideocontext.c - GStreamer/VA video context
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *  Copyright (C) 2013 Igalia
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include "gstvaapivideocontext.h"

#if GST_CHECK_VERSION(1,1,0)

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

#define GST_VAAPI_TYPE_DISPLAY \
  gst_vaapi_display_get_type ()

GType
gst_vaapi_display_get_type (void) G_GNUC_CONST;

G_DEFINE_BOXED_TYPE (GstVaapiDisplay, gst_vaapi_display,
    (GBoxedCopyFunc) gst_vaapi_display_ref,
    (GBoxedFreeFunc) gst_vaapi_display_unref);

GstContext *
gst_vaapi_video_context_new_with_display (GstVaapiDisplay * display,
    gboolean persistent)
{
  GstContext *context;
  GstStructure *structure;

  context = gst_context_new (GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME, persistent);
  structure = gst_context_writable_structure (context);
  gst_structure_set (structure, "display", GST_VAAPI_TYPE_DISPLAY,
      display, NULL);
  return context;
}

gboolean
gst_vaapi_video_context_get_display (GstContext * context,
    GstVaapiDisplay ** display_ptr)
{
  const GstStructure *structure;

  g_return_val_if_fail (GST_IS_CONTEXT (context), FALSE);
  g_return_val_if_fail (g_strcmp0 (gst_context_get_context_type (context),
          GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME) == 0, FALSE);

  structure = gst_context_get_structure (context);
  return gst_structure_get (structure, "display", GST_VAAPI_TYPE_DISPLAY,
      display_ptr, NULL);
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
run_context_query (GstElement * element, GstQuery * query)
{
  GstIteratorFoldFunction const func = context_pad_query;
  GstIterator *it;
  GValue res = { 0 };

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

  /* Ask downstream neighbour */
  it = gst_element_iterate_src_pads (element);
  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);
  gst_iterator_free (it);

  if (g_value_get_boolean (&res))
    return TRUE;

  /* If none, ask upstream neighbour (auto-plugged case) */
  it = gst_element_iterate_sink_pads (element);
  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);
  gst_iterator_free (it);

  return g_value_get_boolean (&res);
}

void
gst_vaapi_video_context_prepare (GstElement * element, const gchar ** types)
{
  GstContext *context;
  GstQuery *query;
  GstMessage *msg;

  if (!GST_CAT_CONTEXT)
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");

  /*  1) Check if the element already has a context of the specific
   *     type, i.e. it was previously set via
   *     gst_element_set_context(). */
  /* This was already done by the caller of this function:
   * gst_vaapi_ensure_display() */

  /* 2) Query downstream with GST_QUERY_CONTEXT for the context and
     check if downstream already has a context of the specific
     type */
  /* 3) Query upstream with GST_QUERY_CONTEXT for the context and
     check if upstream already has a context of the specific
     type */
  context = NULL;
  query = gst_query_new_context (GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME);
  if (run_context_query (element, query)) {
    gst_query_parse_context (query, &context);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in query", context);
    gst_element_set_context (element, context);
  } else {
    /* 4) Post a GST_MESSAGE_NEED_CONTEXT message on the bus with
       the required context types and afterwards check if an
       usable context was set now as in 1). The message could
       be handled by the parent bins of the element and the
       application. */
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting `need-context' message");
    msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME);
    gst_element_post_message (element, msg);

    /* The check of an usable context is done by the caller:
       gst_vaapi_ensure_display() */
  }

  gst_query_unref (query);
}

/* 5) Create a context by itself and post a GST_MESSAGE_HAVE_CONTEXT message
      on the bus. */
void
gst_vaapi_video_context_propagate (GstElement * element,
    GstVaapiDisplay * display)
{
  GstContext *context;
  GstMessage *msg;

  context = gst_vaapi_video_context_new_with_display (display, FALSE);

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting `have-context' (%p) message with display (%p)",
      context, display);
  msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
  gst_element_post_message (GST_ELEMENT_CAST (element), msg);
}

#endif
