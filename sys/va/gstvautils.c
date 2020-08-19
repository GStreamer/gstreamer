/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include "gstvadisplay_drm.h"
#include "gstvadisplay_wrapped.h"
#include "gstvautils.h"

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
gst_va_display_found (GstElement * element, GstVaDisplay * display)
{
  _init_context_debug ();

  if (display) {
    GST_CAT_LOG_OBJECT (GST_CAT_CONTEXT, element, "already have a display (%p)",
        display);
    return TRUE;
  }

  return FALSE;
}

static gboolean
pad_query (const GValue * item, GValue * value, gpointer user_data)
{
  GstPad *pad = g_value_get_object (item);
  GstQuery *query = user_data;
  gboolean res;

  _init_context_debug ();

  res = gst_pad_peer_query (pad, query);

  if (res) {
    g_value_set_boolean (value, TRUE);
    return FALSE;
  }

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, pad, "pad peer query failed");
  return TRUE;
}

static gboolean
_gst_va_run_query (GstElement * element, GstQuery * query,
    GstPadDirection direction)
{
  GstIterator *it;
  GstIteratorFoldFunction func = pad_query;
  GValue res = G_VALUE_INIT;

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

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
_gst_context_query (GstElement * element, const gchar * context_type)
{
  GstQuery *query;
  GstContext *ctxt = NULL;

  _init_context_debug ();

  /*  2a) Query downstream with GST_QUERY_CONTEXT for the context and
   *      check if downstream already has a context of the specific type
   *  2b) Query upstream as above.
   */
  query = gst_query_new_context (context_type);
  if (_gst_va_run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in downstream query", ctxt);
    gst_element_set_context (element, ctxt);
  } else if (_gst_va_run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in upstream query", ctxt);
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
        context_type);
    gst_element_post_message (element, msg);
  }

  /*
   * Whomever responds to the need-context message performs a
   * GstElement::set_context() with the required context in which the element
   * is required to update the display_ptr or call gst_va_handle_set_context().
   */

  gst_query_unref (query);
}

/*  4) Create a context by itself and post a GST_MESSAGE_HAVE_CONTEXT
 *     message.
 */
void
gst_va_element_propagate_display_context (GstElement * element,
    GstVaDisplay * display)
{
  GstContext *ctxt;
  GstMessage *msg;

  if (!display) {
    GST_ERROR_OBJECT (element, "Could not get GL display connection");
    return;
  }

  _init_context_debug ();

  ctxt = gst_context_new ("gst.va.display.handle", TRUE);
  gst_context_set_va_display (ctxt, display);

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "post have context (%p) message with display (%p)", ctxt, display);
  msg = gst_message_new_have_context (GST_OBJECT_CAST (element), ctxt);
  gst_element_post_message (element, msg);
}

gboolean
gst_va_ensure_element_data (gpointer element, const gchar * render_device_path,
    GstVaDisplay ** display_ptr)
{
  GstVaDisplay *display;

  g_return_val_if_fail (element, FALSE);
  g_return_val_if_fail (render_device_path, FALSE);
  g_return_val_if_fail (display_ptr, FALSE);

  /*  1) Check if the element already has a context of the specific
   *     type.
   */
  if (gst_va_display_found (element, *display_ptr))
    goto done;

  _gst_context_query (element, "gst.va.display.handle");

  /* Neighbour found and it updated the display */
  if (gst_va_display_found (element, *display_ptr))
    goto done;

  /* If no neighbor, or application not interested, use drm */
  display = gst_va_display_drm_new_from_path (render_device_path);

  *display_ptr = display;

  gst_va_element_propagate_display_context (element, display);

done:
  return *display_ptr != NULL;
}

gboolean
gst_va_handle_set_context (GstElement * element, GstContext * context,
    const gchar * render_device_path, GstVaDisplay ** display_ptr)
{
  GstVaDisplay *display_replacement = NULL;
  const gchar *context_type, *type_name;

  g_return_val_if_fail (display_ptr, FALSE);

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);

  if (g_strcmp0 (context_type, "gst.va.display.handle") == 0) {
    type_name = G_OBJECT_TYPE_NAME (element);
    if (!gst_context_get_va_display (context, type_name, render_device_path,
            &display_replacement)) {
      GST_CAT_WARNING_OBJECT (GST_CAT_CONTEXT, element,
          "Failed to get display from context");
      return FALSE;
    }
  }

  if (display_replacement) {
    GstVaDisplay *old = *display_ptr;
    *display_ptr = display_replacement;

    if (old)
      gst_object_unref (old);
  }

  return TRUE;
}

gboolean
gst_va_handle_context_query (GstElement * element, GstQuery * query,
    GstVaDisplay * display)
{
  const gchar *context_type;
  GstContext *ctxt, *old_ctxt;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);
  g_return_val_if_fail (!display || GST_IS_VA_DISPLAY (display), FALSE);

  _init_context_debug ();

  GST_CAT_LOG_OBJECT (GST_CAT_CONTEXT, element,
      "handle context query %" GST_PTR_FORMAT, query);
  gst_query_parse_context_type (query, &context_type);

  if (!display || g_strcmp0 (context_type, "gst.va.display.handle") != 0)
    return FALSE;

  gst_query_parse_context (query, &old_ctxt);

  if (old_ctxt)
    ctxt = gst_context_copy (old_ctxt);
  else
    ctxt = gst_context_new ("gst.va.display.handle", TRUE);

  gst_context_set_va_display (ctxt, display);
  gst_query_set_context (query, ctxt);
  gst_context_unref (ctxt);
  GST_CAT_DEBUG_OBJECT (GST_CAT_CONTEXT, element,
      "successuflly %" GST_PTR_FORMAT " on %" GST_PTR_FORMAT, display, query);

  return TRUE;
}

gboolean
gst_context_get_va_display (GstContext * context, const gchar * type_name,
    const gchar * render_device_path, GstVaDisplay ** display_ptr)
{
  const GstStructure *s;
  GstVaDisplay *display = NULL;
  gpointer dpy;
  gboolean is_devnode;

  g_return_val_if_fail (display_ptr, FALSE);
  g_return_val_if_fail (context, FALSE);

  is_devnode = (g_strstr_len (type_name, -1, "renderD") != NULL);

  s = gst_context_get_structure (context);
  if (gst_structure_get (s, "gst-display", GST_TYPE_VA_DISPLAY, &display, NULL)) {
    gchar *device_path = NULL;
    gboolean ret;

    if (GST_IS_VA_DISPLAY_DRM (display)) {
      g_object_get (display, "path", &device_path, NULL);
      ret = (g_strcmp0 (device_path, render_device_path) == 0);
      g_free (device_path);
      if (ret)
        goto accept;
    } else if (!is_devnode) {
      goto accept;
    }

    /* let's try other fields */
    gst_clear_object (&display);
  }

  /* if element is render device node specific, it doesn't accept
   * VADisplay from users */
  if (!is_devnode
      && gst_structure_get (s, "va-display", G_TYPE_POINTER, &dpy, NULL)) {
    if ((display = gst_va_display_wrapped_new ((guintptr) dpy)))
      goto accept;
  }

  GST_CAT_DEBUG (GST_CAT_CONTEXT, "No valid GstVaDisplay from context (%p)",
      context);
  return FALSE;

accept:
  {
    *display_ptr = display;

    GST_CAT_LOG (GST_CAT_CONTEXT, "got GstVaDisplay (%p) from context (%p)",
        *display_ptr, context);
    return TRUE;
  }
}

void
gst_context_set_va_display (GstContext * context, GstVaDisplay * display)
{
  GstStructure *s;

  g_return_if_fail (context != NULL);

  if (display)
    GST_CAT_LOG (GST_CAT_CONTEXT,
        "setting GstVaDisplay (%" GST_PTR_FORMAT ") on context (%"
        GST_PTR_FORMAT ")", display, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, "gst-display", GST_TYPE_VA_DISPLAY, display, NULL);
}
