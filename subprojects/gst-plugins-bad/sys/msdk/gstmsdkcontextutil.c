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
#include <gst/va/gstvadisplay.h>
#include <gst/va/gstvautils.h>

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

/* Find whether the other elements already have a msdk context. */
gboolean
gst_msdk_context_find (GstElement * element, GstMsdkContext ** context_ptr)
{
  _init_context_debug ();

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (context_ptr != NULL, FALSE);

  /* 1) Check if the element already has a context of the specific type. */
  if (*context_ptr) {
    GST_LOG_OBJECT (element, "already have a context %" GST_PTR_FORMAT,
        *context_ptr);
    return TRUE;
  }

  /* This may indirectly set *context_ptr, see function body */
  gst_va_context_query (element, GST_MSDK_CONTEXT_TYPE_NAME);

  if (*context_ptr) {
    GST_LOG_OBJECT (element, "found a context %" GST_PTR_FORMAT, *context_ptr);
    return TRUE;
  }

  return *context_ptr != NULL;
}

gboolean
gst_msdk_context_get_context (GstContext * context,
    GstMsdkContext ** msdk_context)
{
  const GstStructure *structure;
  const gchar *type;

  _init_context_debug ();

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

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting `have-context' message with MSDK context %" GST_PTR_FORMAT,
      msdk_context);

  msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
  if (!gst_element_post_message (element, msg))
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element, "No bus attached");
}

/* When we can not find a suitable context from others, we ensure to create
   a new context. */
gboolean
gst_msdk_ensure_new_context (GstElement * element, gboolean hardware,
    GstMsdkContextJobType job, GstMsdkContext ** context_ptr)
{
  GstMsdkContext *msdk_context;
  gboolean propagate_display = FALSE;
  gboolean ret = FALSE;

  g_return_val_if_fail (element, FALSE);
  g_return_val_if_fail (context_ptr, FALSE);

  _init_context_debug ();

  /* 1) Already have. */
  if (g_atomic_pointer_get (context_ptr))
    return TRUE;

#ifndef _WIN32
  /* 2) Query the neighbour the VA display. If already a valid VA display,
     using it by gst_msdk_context_from_external_display() in set_context(). */
  gst_va_context_query (element, GST_VA_DISPLAY_HANDLE_CONTEXT_TYPE_STR);
  msdk_context = g_atomic_pointer_get (context_ptr);
  if (msdk_context) {
    gst_object_ref (msdk_context);
    propagate_display = FALSE;
    ret = TRUE;
    goto done;
  }
#endif

  /* 3) Create a MSDK context from scratch. */
  msdk_context = gst_msdk_context_new (hardware, job);
  if (!msdk_context) {
    GST_ERROR_OBJECT (element, "Context creation failed");
    return FALSE;
  }
  propagate_display = TRUE;
  ret = TRUE;

  GST_INFO_OBJECT (element, "New MSDK Context %p", msdk_context);

  gst_object_replace ((GstObject **) context_ptr, (GstObject *) msdk_context);

done:
  if (propagate_display) {
#ifndef _WIN32
    GstVaDisplay *display =
        (GstVaDisplay *) gst_msdk_context_get_display (msdk_context);
    gst_va_element_propagate_display_context (element, display);
    gst_clear_object (&display);
#endif
  }

  gst_msdk_context_propagate (element, msdk_context);
  gst_object_unref (msdk_context);

  return ret;
}

gboolean
gst_msdk_context_from_external_display (GstContext * context, gboolean hardware,
    GstMsdkContextJobType job_type, GstMsdkContext ** msdk_context)
{
#ifndef _WIN32
  GstObject *va_display = NULL;
  const gchar *type;
  const GstStructure *s;
  GstMsdkContext *ctx = NULL;

  _init_context_debug ();

  type = gst_context_get_context_type (context);
  if (g_strcmp0 (type, GST_VA_DISPLAY_HANDLE_CONTEXT_TYPE_STR))
    return FALSE;

  s = gst_context_get_structure (context);
  if (gst_structure_get (s, "gst-display", GST_TYPE_OBJECT, &va_display, NULL)) {
    if (GST_IS_VA_DISPLAY (va_display)) {
      /* TODO: Need to check whether the display is the DEV we want. */
      ctx =
          gst_msdk_context_new_with_va_display (va_display, hardware, job_type);
      if (ctx)
        *msdk_context = ctx;
    }

    /* let's try other fields */
    gst_clear_object (&va_display);
  }

  if (ctx)
    return TRUE;

#endif

  return FALSE;
}

gboolean
gst_msdk_handle_context_query (GstElement * element, GstQuery * query,
    GstMsdkContext * msdk_context)
{
  const gchar *context_type;
  GstContext *ctxt, *old_ctxt;
  gboolean ret = FALSE;

  _init_context_debug ();

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);
  g_return_val_if_fail (!msdk_context
      || GST_IS_MSDK_CONTEXT (msdk_context), FALSE);

  GST_CAT_LOG_OBJECT (GST_CAT_CONTEXT, element,
      "handle context query %" GST_PTR_FORMAT, query);

  if (!msdk_context)
    return FALSE;

  gst_query_parse_context_type (query, &context_type);

  gst_query_parse_context (query, &old_ctxt);
  if (old_ctxt)
    ctxt = gst_context_copy (old_ctxt);
  else
    ctxt = gst_context_new (context_type, TRUE);

#ifndef _WIN32
  if (g_strcmp0 (context_type, GST_VA_DISPLAY_HANDLE_CONTEXT_TYPE_STR) == 0) {
    GstStructure *s;
    GstObject *display = gst_msdk_context_get_display (msdk_context);

    if (display) {
      GST_CAT_LOG (GST_CAT_CONTEXT,
          "setting GstVaDisplay (%" GST_PTR_FORMAT ") on context (%"
          GST_PTR_FORMAT ")", display, ctxt);

      s = gst_context_writable_structure (ctxt);
      gst_structure_set (s, "gst-display", GST_TYPE_OBJECT, display, NULL);
      /* Structure hold one ref */
      gst_object_unref (display);
      ret = TRUE;
    }
  } else
#endif
  if (g_strcmp0 (context_type, GST_MSDK_CONTEXT_TYPE_NAME) == 0) {
    GstStructure *s;

    s = gst_context_writable_structure (ctxt);
    GST_CAT_LOG (GST_CAT_CONTEXT,
        "setting GstMsdkContext (%" GST_PTR_FORMAT ") on context (%"
        GST_PTR_FORMAT ")", msdk_context, ctxt);
    gst_structure_set (s, GST_MSDK_CONTEXT_TYPE_NAME, GST_TYPE_MSDK_CONTEXT,
        msdk_context, NULL);
    ret = TRUE;
  }

  if (ret)
    gst_query_set_context (query, ctxt);

  gst_context_unref (ctxt);
  return ret;
}
