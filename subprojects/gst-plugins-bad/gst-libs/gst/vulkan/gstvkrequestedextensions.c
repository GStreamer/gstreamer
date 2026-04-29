/*
 * GStreamer
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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

#include "gstvkrequestedextensions.h"
#include "gstvkutils.h"
#include "gstvkinstance.h"

#define EXTENSIONS_STRV_KEY "extensions"


static gboolean
_is_instance_requested_ext_context_type (const gchar * context_type)
{
  return g_strcmp0 (context_type,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR) == 0;
}

static gboolean
_is_device_requested_ext_context_type (const gchar * context_type)
{
  return g_strcmp0 (context_type,
      GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR) == 0;
}

static gboolean
_requested_ext_context_type_valid (const gchar * context_type)
{
  return _is_instance_requested_ext_context_type (context_type)
      || _is_device_requested_ext_context_type (context_type);
}

/* Append @value to the existing G_TYPE_STRV @field_name of @s, deduplicating. */
static void
_strv_field_append_unique (GstStructure * s, const gchar * field_name,
    const gchar * value)
{
  const GValue *val;
  gchar **existing = NULL;
  gchar **out;
  guint n_existing = 0;
  guint i;

  if (!value)
    return;

  val = gst_structure_get_value (s, field_name);
  if (val && G_VALUE_HOLDS (val, G_TYPE_STRV)) {
    existing = (gchar **) g_value_get_boxed (val);
    if (existing) {
      for (i = 0; existing[i]; i++) {
        if (g_strcmp0 (existing[i], value) == 0)
          return;
      }
      n_existing = i;
    }
  }

  out = g_new (gchar *, n_existing + 2);
  for (i = 0; i < n_existing; i++)
    out[i] = g_strdup (existing[i]);
  out[n_existing] = g_strdup (value);
  out[n_existing + 1] = NULL;

  gst_structure_set (s, field_name, G_TYPE_STRV, out, NULL);
  g_strfreev (out);
}

static gchar **
_strv_field_get_copy (GstStructure * s, const gchar * field_name)
{
  const GValue *val;
  gchar **existing;

  val = gst_structure_get_value (s, field_name);
  if (!val || !G_VALUE_HOLDS (val, G_TYPE_STRV))
    return NULL;

  existing = (gchar **) g_value_get_boxed (val);
  if (!existing)
    return NULL;

  return g_strdupv (existing);
}

static void
_merge_requested_context_vulkan_instance (GstContext * dst, GstContext * src)
{
  GstVulkanInstance *src_inst = NULL;
  GstVulkanInstance *dst_inst = NULL;

  if (!gst_context_get_vulkan_instance (src, &src_inst))
    return;

  if (!src_inst)
    return;

  if (gst_context_get_vulkan_instance (dst, &dst_inst) && dst_inst) {
    gst_object_unref (dst_inst);
    gst_object_unref (src_inst);
    return;
  }

  if (!gst_context_is_writable (dst)) {
    gst_object_unref (src_inst);
    return;
  }

  gst_context_set_vulkan_instance (dst, src_inst);
  gst_object_unref (src_inst);
}

static void
_merge_same_type_context (GstContext * dst, GstContext * src)
{
  gchar **ext, **p;
  const gchar *ctype;

  if (!src || dst == src)
    return;

  if (g_strcmp0 (gst_context_get_context_type (dst),
          gst_context_get_context_type (src)) != 0)
    return;

  ctype = gst_context_get_context_type (dst);

  if (_is_device_requested_ext_context_type (ctype)) {
    GstVulkanInstance *src_inst = NULL;
    GstVulkanInstance *dst_inst = NULL;
    gboolean got_src = gst_context_get_vulkan_instance (src, &src_inst);
    gboolean got_dst = gst_context_get_vulkan_instance (dst, &dst_inst);

    /* Different non-NULL instances: do not merge extension names or instance. */
    if (got_src && src_inst && got_dst && dst_inst && src_inst != dst_inst) {
      gst_clear_object (&src_inst);
      gst_clear_object (&dst_inst);
      return;
    }

    /* Device requests must be anchored on a #GstVulkanInstance: ignore
     * extension-only fragments when neither side identifies the instance yet. */
    if ((!got_src || !src_inst) && (!got_dst || !dst_inst)) {
      gchar **orphan_ext =
          gst_vulkan_requested_extensions_context_dup_extensions (src);
      gboolean src_has_ext = orphan_ext && orphan_ext[0];
      g_strfreev (orphan_ext);
      gst_clear_object (&src_inst);
      gst_clear_object (&dst_inst);
      if (src_has_ext)
        return;
    } else {
      gst_clear_object (&src_inst);
      gst_clear_object (&dst_inst);
    }
  }

  ext = gst_vulkan_requested_extensions_context_dup_extensions (src);
  if (ext) {
    for (p = ext; *p; p++)
      gst_vulkan_requested_extensions_context_add (dst, *p);
    g_strfreev (ext);
  }

  if (_is_device_requested_ext_context_type (ctype))
    _merge_requested_context_vulkan_instance (dst, src);
}

/**
 * gst_vulkan_requested_instance_extensions_context_new:
 *
 * Returns: (transfer full): new instance-extension request #GstContext
 *
 * Since: 1.30
 */
GstContext *
gst_vulkan_requested_instance_extensions_context_new (void)
{
  return
      gst_context_new
      (GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR, TRUE);
}

/**
 * gst_vulkan_requested_device_extensions_context_new:
 *
 * Returns: (transfer full): new device-extension request #GstContext
 *
 * Since: 1.30
 */
GstContext *
gst_vulkan_requested_device_extensions_context_new (void)
{
  return
      gst_context_new (GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR,
      TRUE);
}

/**
 * gst_vulkan_requested_extensions_context_add:
 * @context: a #GstContext of type
 *   %GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR or
 *   %GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR
 * @extension_name: Vulkan extension name to request
 *
 * Appends @extension_name to `extensions`, ignoring duplicates.
 *
 * Since: 1.30
 */
void
gst_vulkan_requested_extensions_context_add (GstContext * context,
    const gchar * extension_name)
{
  GstStructure *s;

  g_return_if_fail (context != NULL);
  g_return_if_fail (gst_context_is_writable (context));
  g_return_if_fail (_requested_ext_context_type_valid
      (gst_context_get_context_type (context)));
  g_return_if_fail (extension_name != NULL);

  s = gst_context_writable_structure (context);
  _strv_field_append_unique (s, EXTENSIONS_STRV_KEY, extension_name);
}

/**
 * gst_vulkan_requested_extensions_context_dup_extensions:
 * @context: a requested-extensions #GstContext
 *
 * Returns: (transfer full) (nullable) (array zero-terminated=1): a copy of
 * the `extensions` %NULL-terminated list, or %NULL if unset/empty.
 *
 * Since: 1.30
 */
gchar **
gst_vulkan_requested_extensions_context_dup_extensions (GstContext * context)
{
  GstStructure *s;

  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (_requested_ext_context_type_valid
      (gst_context_get_context_type (context)), NULL);

  s = (GstStructure *) gst_context_get_structure (context);
  return _strv_field_get_copy (s, EXTENSIONS_STRV_KEY);
}

/**
 * gst_vulkan_requested_extensions_context_set_vulkan_instance:
 * @context: a requested-extension #GstContext
 * @instance: (transfer none) (nullable): #GstVulkanInstance, or %NULL to clear
 *
 * Sets or clears the embedded #GstVulkanInstance on @context, using
 * %GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR in the #GstStructure (same as
 * gst_context_set_vulkan_instance()). This is only meaningful for contexts of
 * type %GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR (the instance
 * type ignores the field when merging).
 *
 * Since: 1.30
 */
void
gst_vulkan_requested_extensions_context_set_vulkan_instance (GstContext *
    context, GstVulkanInstance * instance)
{
  g_return_if_fail (context != NULL);
  g_return_if_fail (gst_context_is_writable (context));
  g_return_if_fail (_requested_ext_context_type_valid
      (gst_context_get_context_type (context)));

  gst_context_set_vulkan_instance (context, instance);
}

/**
 * gst_vulkan_requested_extensions_context_get_vulkan_instance:
 * @context: a requested-extension #GstContext
 * @instance: (out) (optional) (nullable) (transfer full): location for the result
 *
 * Returns: %TRUE if @instance was present in @context
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_requested_extensions_context_get_vulkan_instance (GstContext *
    context, GstVulkanInstance ** instance)
{
  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (instance != NULL, FALSE);
  g_return_val_if_fail (_requested_ext_context_type_valid
      (gst_context_get_context_type (context)), FALSE);

  return gst_context_get_vulkan_instance (context, instance);
}

/**
 * gst_vulkan_requested_extensions_merge_from_element:
 * @element: a #GstElement
 * @dst: writable #GstContext, instance or device requested-extensions type
 *
 * Merges every attached context matching @dst's type into @dst (extension
 * names, and for device contexts only, the embedded #GstVulkanInstance when
 * @dst does not carry one yet). For device contexts, extension-only sources
 * are ignored until an instance anchor exists, and contexts naming different
 * non-NULL #GstVulkanInstance objects do not contribute to @dst.
 *
 * Since: 1.30
 */
void
gst_vulkan_requested_extensions_merge_from_element (GstElement * element,
    GstContext * dst)
{
  GList *contexts, *l;
  const gchar *dst_type;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (dst != NULL);
  g_return_if_fail (gst_context_is_writable (dst));

  dst_type = gst_context_get_context_type (dst);
  g_return_if_fail (_requested_ext_context_type_valid (dst_type));

  contexts = gst_element_get_contexts (element);
  for (l = contexts; l; l = l->next) {
    GstContext *ctx = (GstContext *) l->data;

    if (g_strcmp0 (gst_context_get_context_type (ctx), dst_type) != 0)
      continue;

    _merge_same_type_context (dst, ctx);
  }
  g_list_free_full (contexts, (GDestroyNotify) gst_context_unref);
}

static gboolean
_context_merge_result_is_actionable (GstContext * ctx,
    const gchar * context_type)
{
  const GValue *val;
  gboolean has_ext = FALSE;

  if (!ctx)
    return FALSE;

  val = gst_structure_get_value (gst_context_get_structure (ctx),
      EXTENSIONS_STRV_KEY);
  if (val && G_VALUE_HOLDS (val, G_TYPE_STRV)) {
    gchar **ext = (gchar **) g_value_get_boxed (val);
    if (ext && ext[0])
      has_ext = TRUE;
  }

  if (_is_device_requested_ext_context_type (context_type)) {
    GstVulkanInstance *inst = NULL;
    gboolean has_inst = FALSE;
    if (gst_vulkan_requested_extensions_context_get_vulkan_instance (ctx,
            &inst)) {
      if (inst) {
        has_inst = TRUE;
        gst_object_unref (inst);
      }
    }
    return has_ext && has_inst;
  }

  return has_ext;
}

static GstContext *
_element_get_merged_one_type (GstElement * element, const gchar * type_str)
{
  GstContext *merged;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (_requested_ext_context_type_valid (type_str), NULL);

  merged = gst_context_new (type_str, TRUE);
  gst_vulkan_requested_extensions_merge_from_element (element, merged);

  if (!_context_merge_result_is_actionable (merged, type_str)) {
    gst_context_unref (merged);
    return NULL;
  }

  return merged;
}

/**
 * gst_vulkan_element_get_merged_requested_instance_extensions_context:
 * @element: a #GstElement
 *
 * Merges every %GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR
 * context already set on @element into a single #GstContext (extension names
 * only; see gst_vulkan_requested_extensions_merge_from_element()).
 *
 * Returns: (transfer full) (nullable): merged context, or %NULL if no
 * instance extension names were found
 *
 * Since: 1.30
 */
GstContext *
gst_vulkan_element_get_merged_requested_instance_extensions_context (GstElement
    * element)
{
  return _element_get_merged_one_type (element,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);
}

/**
 * gst_vulkan_element_get_merged_requested_device_extensions_context:
 * @element: a #GstElement
 *
 * Like gst_vulkan_element_get_merged_requested_instance_extensions_context(),
 * but for %GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR. The result
 * is only non-%NULL if the merge includes a #GstVulkanInstance anchor
 * (extension-only device requests are ignored).
 *
 * Returns: (transfer full) (nullable): merged context, or %NULL
 *
 * Since: 1.30
 */
GstContext *
gst_vulkan_element_get_merged_requested_device_extensions_context (GstElement *
    element)
{
  return _element_get_merged_one_type (element,
      GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR);
}

static void
_requested_ext_context_propagate (GstElement * element, GstContext * context)
{
  GstMessage *msg;

  gst_element_set_context (element, context);

  GST_INFO_OBJECT (element,
      "posting have context (%" GST_PTR_FORMAT ") message for %s", context,
      gst_context_get_context_type (context));
  msg = gst_message_new_have_context (GST_OBJECT_CAST (element),
      gst_context_ref (context));
  gst_element_post_message (element, msg);
}

static gboolean
_merge_peer_direction_one_type (GstElement * element,
    GstPadDirection direction, GstContext * merged, const gchar * type_str)
{
  GstQuery *query;
  gboolean ok;

  query = gst_query_new_context (type_str);
  ok = gst_vulkan_run_query (element, query, direction);
  if (ok) {
    GstContext *peer_ctx = NULL;

    gst_query_parse_context (query, &peer_ctx);
    if (peer_ctx)
      _merge_same_type_context (merged, peer_ctx);
  }
  gst_query_unref (query);

  return ok;
}

static GstQuery *
_requested_extensions_local_context_query (GstElement * element,
    const gchar * context_type, GstVulkanInstance * instance,
    gboolean set_context)
{
  GstContext *merged;
  GstQuery *query;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (context_type != NULL, NULL);
  g_return_val_if_fail (_requested_ext_context_type_valid (context_type), NULL);
  if (_is_device_requested_ext_context_type (context_type))
    g_return_val_if_fail (instance != NULL, NULL);

  merged = gst_context_new (context_type, TRUE);

  if (_is_device_requested_ext_context_type (context_type))
    gst_context_set_vulkan_instance (merged, instance);

  gst_vulkan_requested_extensions_merge_from_element (element, merged);

  _merge_peer_direction_one_type (element, GST_PAD_SRC, merged, context_type);
  _merge_peer_direction_one_type (element, GST_PAD_SINK, merged, context_type);

  if (!_context_merge_result_is_actionable (merged, context_type)) {
    gst_context_unref (merged);
    return NULL;
  }

  if (set_context)
    gst_element_set_context (element, merged);

  query = gst_query_new_context (context_type);
  gst_query_set_context (query, merged);
  gst_context_unref (merged);

  return query;
}

/**
 * gst_vulkan_requested_extensions_local_context_query:
 * @element: a #GstElement
 * @context_type: %GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR or
 *   %GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR
 * @instance: (nullable): the #GstVulkanInstance for device negotiation; required
 *     when @context_type is
 *     %GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR, otherwise
 *     ignored (%NULL)
 *
 * Like gst_vulkan_local_context_query(), but merges requested-extension contexts
 * from both peer directions and from contexts already set on @element.
 *
 * Returns: (transfer full) (nullable): a #GST_QUERY_CONTEXT carrying the merged
 * context when actionable, or %NULL. Does not post bus messages or call
 * gst_element_set_context().
 *
 * Since: 1.30
 */
GstQuery *
gst_vulkan_requested_extensions_local_context_query (GstElement * element,
    const gchar * context_type, GstVulkanInstance * instance)
{
  return _requested_extensions_local_context_query (element, context_type,
      instance, FALSE);
}

/**
 * gst_vulkan_requested_extensions_global_context_query:
 * @element: a #GstElement
 * @context_type: %GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR
 *
 * Runs the local query (merging both peer directions), always posts
 * %GST_MESSAGE_NEED_CONTEXT so the application or parent bin can contribute
 * extension names, re-merges local contexts after that message, then
 * gst_element_set_context() and %GST_MESSAGE_HAVE_CONTEXT when the merge contains
 * extension names.
 *
 * Since: 1.30
 */
void
gst_vulkan_requested_extensions_global_context_query (GstElement * element,
    const gchar * context_type)
{
  GstQuery *query;
  GstMessage *msg;
  GstContext *merged;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (context_type != NULL);
  g_return_if_fail (_is_instance_requested_ext_context_type (context_type));

  query =
      _requested_extensions_local_context_query (element, context_type, NULL,
      TRUE);
  if (query)
    gst_query_unref (query);

  GST_INFO_OBJECT (element,
      "posting need context message for %s", context_type);
  msg = gst_message_new_need_context (GST_OBJECT_CAST (element), context_type);
  gst_element_post_message (element, msg);

  merged = gst_context_new (context_type, TRUE);
  gst_vulkan_requested_extensions_merge_from_element (element, merged);
  if (_context_merge_result_is_actionable (merged, context_type))
    _requested_ext_context_propagate (element, merged);
  gst_context_unref (merged);
}

/**
 * gst_vulkan_requested_extensions_handle_context_query:
 *
 * Handles #GST_QUERY_CONTEXT for either requested-extensions context type.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_requested_extensions_handle_context_query (GstElement * element,
    GstQuery * query, GstPadDirection continue_direction,
    GstVulkanInstance * instance)
{
  const gchar *context_type;
  GstContext *old_ctx = NULL;
  GstContext *ctx;
  GstQuery *subq;
  gboolean ret;
  gboolean is_device_ext_context;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT, FALSE);
  g_return_val_if_fail (continue_direction == GST_PAD_SRC
      || continue_direction == GST_PAD_SINK, FALSE);

  gst_query_parse_context_type (query, &context_type);
  if (!_requested_ext_context_type_valid (context_type))
    return FALSE;

  is_device_ext_context = _is_device_requested_ext_context_type (context_type);

  if (is_device_ext_context && !instance)
    return FALSE;

  gst_query_parse_context (query, &old_ctx);
  if (old_ctx)
    ctx = gst_context_copy (old_ctx);
  else
    ctx = gst_context_new (context_type, TRUE);

  if (is_device_ext_context)
    gst_context_set_vulkan_instance (ctx, instance);

  subq = gst_query_new_context (context_type);
  ret = gst_vulkan_run_query (element, subq, continue_direction);
  if (ret) {
    GstContext *peer_ctx = NULL;

    gst_query_parse_context (subq, &peer_ctx);
    if (peer_ctx)
      _merge_same_type_context (ctx, peer_ctx);
  }
  gst_query_unref (subq);

  gst_vulkan_requested_extensions_merge_from_element (element, ctx);

  gst_query_set_context (query, ctx);
  gst_context_unref (ctx);

  return TRUE;
}
