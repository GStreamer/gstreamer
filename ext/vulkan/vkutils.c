/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include "vkutils.h"
#include "vkutils_private.h"

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

gboolean
_check_for_all_layers (uint32_t check_count, const char **check_names,
    uint32_t layer_count, VkLayerProperties * layers)
{
  uint32_t i, j;

  for (i = 0; i < check_count; i++) {
    gboolean found = FALSE;
    for (j = 0; j < layer_count; j++) {
      if (g_strcmp0 (check_names[i], layers[j].layerName) == 0) {
        found = TRUE;
      }
    }
    if (!found) {
      GST_ERROR ("Cannot find layer: %s", check_names[i]);
      return FALSE;
    }
  }
  return TRUE;
}

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
_vk_pad_query (const GValue * item, GValue * value, gpointer user_data)
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

gboolean
gst_vulkan_run_query (GstElement * element, GstQuery * query,
    GstPadDirection direction)
{
  GstIterator *it;
  GstIteratorFoldFunction func = _vk_pad_query;
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
_vk_gst_context_query (GstElement * element, const gchar * display_type)
{
  GstQuery *query;
  GstContext *ctxt;

  _init_context_debug ();

  /*  2a) Query downstream with GST_QUERY_CONTEXT for the context and
   *      check if downstream already has a context of the specific type
   *  2b) Query upstream as above.
   */
  query = gst_query_new_context (display_type);
  if (gst_vulkan_run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in downstream query", ctxt);
    gst_element_set_context (element, ctxt);
  } else if (gst_vulkan_run_query (element, query, GST_PAD_SINK)) {
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
        display_type);
    gst_element_post_message (element, msg);
  }

  /*
   * Whomever responds to the need-context message performs a
   * GstElement::set_context() with the required context in which the element
   * is required to update the display_ptr or call gst_vulkan_handle_set_context().
   */

  gst_query_unref (query);
}

static void
_vk_display_context_query (GstElement * element,
    GstVulkanDisplay ** display_ptr)
{
  _vk_gst_context_query (element, GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR);
}

/*  4) Create a context by itself and post a GST_MESSAGE_HAVE_CONTEXT
 *     message.
 */
/*
 * @element: (transfer none):
 * @context: (transfer full):
 */
static void
_vk_context_propagate (GstElement * element, GstContext * context)
{
  GstMessage *msg;

  _init_context_debug ();

  gst_element_set_context (element, context);

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
      "posting have context (%" GST_PTR_FORMAT ") message", context);
  msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
  gst_element_post_message (GST_ELEMENT_CAST (element), msg);
}

gboolean
gst_vulkan_ensure_element_data (gpointer element,
    GstVulkanDisplay ** display_ptr, GstVulkanInstance ** instance_ptr)
{
  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (display_ptr != NULL, FALSE);
  g_return_val_if_fail (instance_ptr != NULL, FALSE);

  /*  1) Check if the element already has a context of the specific
   *     type.
   */
  if (!*instance_ptr) {
    GError *error = NULL;

    _vk_gst_context_query (element, GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR);

    /* Neighbour found and it updated the display */
    if (!*instance_ptr) {
      GstContext *context;

      /* If no neighboor, or application not interested, use system default */
      *instance_ptr = gst_vulkan_instance_new ();

      context = gst_context_new (GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR, TRUE);
      gst_context_set_vulkan_instance (context, *instance_ptr);

      _vk_context_propagate (element, context);
    }

    if (!gst_vulkan_instance_open (*instance_ptr, &error)) {
      GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
          ("Failed to create vulkan instance"), ("%s", error->message));
      return FALSE;
    }
  }

  if (!*display_ptr) {
    _vk_display_context_query (element, display_ptr);

    /* Neighbour found and it updated the display */
    if (!*display_ptr) {
      GstContext *context;

      /* instance is required before the display */
      g_return_val_if_fail (*instance_ptr != NULL, FALSE);

      /* If no neighboor, or application not interested, use system default */
      *display_ptr = gst_vulkan_display_new (*instance_ptr);

      context = gst_context_new (GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR, TRUE);
      gst_context_set_vulkan_display (context, *display_ptr);

      _vk_context_propagate (element, context);
    }
  }

  return *display_ptr != NULL && *instance_ptr != NULL;
}

gboolean
gst_vulkan_handle_set_context (GstElement * element, GstContext * context,
    GstVulkanDisplay ** display, GstVulkanInstance ** instance)
{
  GstVulkanDisplay *display_replacement = NULL;
  GstVulkanInstance *instance_replacement = NULL;
  const gchar *context_type;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (instance != NULL, FALSE);

  if (!context)
    return FALSE;

  context_type = gst_context_get_context_type (context);

  if (g_strcmp0 (context_type, GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR) == 0) {
    if (!gst_context_get_vulkan_display (context, &display_replacement)) {
      GST_WARNING_OBJECT (element, "Failed to get display from context");
      return FALSE;
    }
  } else if (g_strcmp0 (context_type,
          GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR) == 0) {
    if (!gst_context_get_vulkan_instance (context, &instance_replacement)) {
      GST_WARNING_OBJECT (element, "Failed to get instance from context");
      return FALSE;
    }
  }

  if (display_replacement) {
    GstVulkanDisplay *old = *display;
    *display = display_replacement;

    if (old)
      gst_object_unref (old);
  }

  if (instance_replacement) {
    GstVulkanInstance *old = *instance;
    *instance = instance_replacement;

    if (old)
      gst_object_unref (old);
  }

  return TRUE;
}

gboolean
gst_vulkan_handle_context_query (GstElement * element, GstQuery * query,
    GstVulkanDisplay ** display, GstVulkanInstance ** instance)
{
  gboolean res = FALSE;
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT, FALSE);
  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (instance != NULL, FALSE);

  gst_query_parse_context_type (query, &context_type);

  if (g_strcmp0 (context_type, GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR) == 0) {
    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR, TRUE);

    gst_context_set_vulkan_display (context, *display);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = *display != NULL;
  } else if (g_strcmp0 (context_type,
          GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR) == 0) {
    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR, TRUE);

    gst_context_set_vulkan_instance (context, *instance);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = *instance != NULL;
  }

  return res;
}
