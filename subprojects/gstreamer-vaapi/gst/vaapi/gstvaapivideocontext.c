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

#include "gstcompat.h"
#include "gstvaapivideocontext.h"
#if USE_GST_GL_HELPERS
# include <gst/gl/gl.h>
#endif
#if GST_VAAPI_USE_X11
#include <gst/vaapi/gstvaapidisplay_x11.h>
#endif
#if GST_VAAPI_USE_WAYLAND
#include <gst/vaapi/gstvaapidisplay_wayland.h>
#endif
#if GST_VAAPI_USE_DRM
#include <gst/vaapi/gstvaapidisplay_drm.h>
#endif

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

static void
_init_context_debug (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
#endif
}

void
gst_vaapi_video_context_set_display (GstContext * context,
    GstVaapiDisplay * display)
{
  GstStructure *structure;

  g_return_if_fail (context != NULL);

  structure = gst_context_writable_structure (context);
  gst_structure_set (structure, GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME,
      GST_TYPE_VAAPI_DISPLAY, display, NULL);
  /* The outside user may access it as a generic Gobject. */
  gst_structure_set (structure, "gst.vaapi.Display.GObject",
      GST_TYPE_OBJECT, display, NULL);
}

GstContext *
gst_vaapi_video_context_new_with_display (GstVaapiDisplay * display,
    gboolean persistent)
{
  GstContext *context;

  context = gst_context_new (GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME, persistent);
  gst_vaapi_video_context_set_display (context, display);
  return context;
}

gboolean
gst_vaapi_video_context_get_display (GstContext * context, gboolean app_context,
    GstVaapiDisplay ** display_ptr)
{
  const GstStructure *structure;
  const gchar *type;
  GstVaapiDisplay *display = NULL;

  g_return_val_if_fail (GST_IS_CONTEXT (context), FALSE);

  type = gst_context_get_context_type (context);

  if (!g_strcmp0 (type, GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME)) {
    structure = gst_context_get_structure (context);
    return gst_structure_get (structure, GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME,
        GST_TYPE_VAAPI_DISPLAY, display_ptr, NULL);
  }

  if (app_context && !g_strcmp0 (type, GST_VAAPI_DISPLAY_APP_CONTEXT_TYPE_NAME)) {
    VADisplay va_display = NULL;
    structure = gst_context_get_structure (context);

    if (gst_structure_get (structure, "va-display", G_TYPE_POINTER, &va_display,
            NULL)) {
#if GST_VAAPI_USE_X11
      Display *x11_display = NULL;
      if (gst_structure_get (structure, "x11-display", G_TYPE_POINTER,
              &x11_display, NULL)) {
        display =
            gst_vaapi_display_x11_new_with_va_display (va_display, x11_display);
      }
#endif
#if GST_VAAPI_USE_WAYLAND
      if (!display) {
        struct wl_display *wl_display = NULL;
        if (gst_structure_get (structure, "wl-display", G_TYPE_POINTER,
                &wl_display, NULL)) {
          display =
              gst_vaapi_display_wayland_new_with_va_display (va_display,
              wl_display);
        }
      }
#endif
#if GST_VAAPI_USE_DRM
      if (!display) {
        gint fd = -1;
        if (gst_structure_get (structure, "drm-device-fd", G_TYPE_INT, &fd,
                NULL)) {
          display = gst_vaapi_display_drm_new_with_va_display (va_display, fd);
        }
      }
#endif

      _init_context_debug ();

      if (!display) {
        GST_CAT_WARNING (GST_CAT_CONTEXT,
            "Cannot create GstVaapiDisplay if only VADisplay is provided");
        return FALSE;
      }
      GST_CAT_INFO (GST_CAT_CONTEXT,
          "new display with context %" GST_PTR_FORMAT, display);
      *display_ptr = display;
      return TRUE;
    }
  }

  return FALSE;
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

  _init_context_debug ();
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

  _init_context_debug ();

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

  /*
   * Whomever responds to the need-context message performs a
   * GstElement::set_context() with the required context in which the
   * element is required to update the display_ptr
   */

found:
  gst_query_unref (query);
}

static gboolean
_gst_vaapi_sink_find_context (GstElement * element)
{
  GstQuery *query;
  GstMessage *msg;
  gboolean found;

  /* 1. Query upstream for an already created GstVaapiDisplay */
  query = gst_query_new_context (GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME);
  found = _gst_context_get_from_query (element, query, GST_PAD_SINK);
  gst_query_unref (query);
  if (found)
    return TRUE;

  /* 2. Post a GST_MESSAGE_NEED_CONTEXT message on the bus with a
   * gst.vaapi.app.Display context from the application */
  msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
      GST_VAAPI_DISPLAY_APP_CONTEXT_TYPE_NAME);
  if (!gst_element_post_message (element, msg)) {
    _init_context_debug ();
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element, "No bus attached");
  }

  return FALSE;
}

gboolean
gst_vaapi_video_context_prepare (GstElement * element,
    GstVaapiDisplay ** display_ptr)
{
  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (display_ptr != NULL, FALSE);

  /*  1) Check if the element already has a context of the specific
   *     type.
   */
  if (*display_ptr) {
    GST_LOG_OBJECT (element, "already have a display %" GST_PTR_FORMAT,
        *display_ptr);
    return TRUE;
  }

  if (GST_IS_VIDEO_SINK (element)) {
    if (!_gst_vaapi_sink_find_context (element) && *display_ptr) {
      /* Propagate if display was created from application */
      gst_vaapi_video_context_propagate (element, *display_ptr);
    }
  } else {
    _gst_context_query (element, GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME);
  }

  if (*display_ptr) {
    GST_LOG_OBJECT (element, "found a display %" GST_PTR_FORMAT, *display_ptr);
    return TRUE;
  }

  return FALSE;
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
  gst_element_set_context (element, context);

  _init_context_debug ();
  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting `have-context' (%p) message with display %" GST_PTR_FORMAT,
      context, display);
  msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
  if (!gst_element_post_message (element, msg))
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element, "No bus attached");
}

/**
 * gst_vaapi_find_gl_local_context:
 * @element: the #GstElement where the search begins
 * @gl_context_ptr: the pointer where the GstGL context is going to be
 * stored
 *
 * Query the pipeline, downstream and upstream for a GstGL context
 *
 * Returns: %TRUE if found; otherwise %FALSE
 **/
gboolean
gst_vaapi_find_gl_local_context (GstElement * element,
    GstObject ** gl_context_ptr)
{
#if USE_GST_GL_HELPERS
  GstGLContext **context_ptr = (GstGLContext **) gl_context_ptr;

  if (gst_gl_query_local_gl_context (element, GST_PAD_SRC, context_ptr))
    return TRUE;
  if (gst_gl_query_local_gl_context (element, GST_PAD_SINK, context_ptr))
    return TRUE;
#endif
  return FALSE;
}
