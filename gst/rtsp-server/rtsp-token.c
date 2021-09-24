/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans at gmail.com>
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
/**
 * SECTION:rtsp-token
 * @short_description: Roles and permissions for a client
 * @see_also: #GstRTSPClient, #GstRTSPPermissions, #GstRTSPAuth
 *
 * A #GstRTSPToken contains the permissions and roles of the user
 * performing the current request. A token is usually created when a user is
 * authenticated by the #GstRTSPAuth object and is then placed as the current
 * token for the current request.
 *
 * #GstRTSPAuth can use the token and its contents to check authorization for
 * various operations by comparing the token to the #GstRTSPPermissions of the
 * object.
 *
 * The accepted values of the token are entirely defined by the #GstRTSPAuth
 * object that implements the security policy.
 *
 * Last reviewed on 2013-07-15 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-token.h"

typedef struct _GstRTSPTokenImpl
{
  GstRTSPToken token;

  GstStructure *structure;
} GstRTSPTokenImpl;

#define GST_RTSP_TOKEN_STRUCTURE(t)  (((GstRTSPTokenImpl *)(t))->structure)

//GST_DEBUG_CATEGORY_STATIC (rtsp_token_debug);
//#define GST_CAT_DEFAULT rtsp_token_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstRTSPToken, gst_rtsp_token);

static void gst_rtsp_token_init (GstRTSPTokenImpl * token,
    GstStructure * structure);

static void
_gst_rtsp_token_free (GstRTSPToken * token)
{
  GstRTSPTokenImpl *impl = (GstRTSPTokenImpl *) token;

  gst_structure_set_parent_refcount (impl->structure, NULL);
  gst_structure_free (impl->structure);

  g_slice_free1 (sizeof (GstRTSPTokenImpl), token);
}

static GstRTSPToken *
_gst_rtsp_token_copy (GstRTSPTokenImpl * token)
{
  GstRTSPTokenImpl *copy;
  GstStructure *structure;

  structure = gst_structure_copy (token->structure);

  copy = g_slice_new0 (GstRTSPTokenImpl);
  gst_rtsp_token_init (copy, structure);

  return (GstRTSPToken *) copy;
}

static void
gst_rtsp_token_init (GstRTSPTokenImpl * token, GstStructure * structure)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (token), 0,
      GST_TYPE_RTSP_TOKEN,
      (GstMiniObjectCopyFunction) _gst_rtsp_token_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_rtsp_token_free);

  token->structure = structure;
  gst_structure_set_parent_refcount (token->structure,
      &token->token.mini_object.refcount);
}

/**
 * gst_rtsp_token_new_empty: (rename-to gst_rtsp_token_new)
 *
 * Create a new empty Authorization token.
 *
 * Returns: (transfer full): a new empty authorization token.
 */
GstRTSPToken *
gst_rtsp_token_new_empty (void)
{
  GstRTSPTokenImpl *token;
  GstStructure *s;

  s = gst_structure_new_empty ("GstRTSPToken");
  g_return_val_if_fail (s != NULL, NULL);

  token = g_slice_new0 (GstRTSPTokenImpl);
  gst_rtsp_token_init (token, s);

  return (GstRTSPToken *) token;
}

/**
 * gst_rtsp_token_new: (skip)
 * @firstfield: the first fieldname
 * @...: additional arguments
 *
 * Create a new Authorization token with the given fieldnames and values.
 * Arguments are given similar to gst_structure_new().
 *
 * Returns: (transfer full): a new authorization token.
 */
GstRTSPToken *
gst_rtsp_token_new (const gchar * firstfield, ...)
{
  GstRTSPToken *result;
  va_list var_args;

  va_start (var_args, firstfield);
  result = gst_rtsp_token_new_valist (firstfield, var_args);
  va_end (var_args);

  return result;
}

/**
 * gst_rtsp_token_new_valist: (skip)
 * @firstfield: the first fieldname
 * @var_args: additional arguments
 *
 * Create a new Authorization token with the given fieldnames and values.
 * Arguments are given similar to gst_structure_new_valist().
 *
 * Returns: (transfer full): a new authorization token.
 */
GstRTSPToken *
gst_rtsp_token_new_valist (const gchar * firstfield, va_list var_args)
{
  GstRTSPToken *token;
  GstStructure *s;

  g_return_val_if_fail (firstfield != NULL, NULL);

  token = gst_rtsp_token_new_empty ();
  s = GST_RTSP_TOKEN_STRUCTURE (token);
  gst_structure_set_valist (s, firstfield, var_args);

  return token;
}

/**
 * gst_rtsp_token_set_string:
 * @token: The #GstRTSPToken.
 * @field: field to set
 * @string_value: string value to set
 *
 * Sets a string value on @token.
 *
 * Since: 1.14
 */
void
gst_rtsp_token_set_string (GstRTSPToken * token, const gchar * field,
    const gchar * string_value)
{
  GstStructure *s;

  g_return_if_fail (token != NULL);
  g_return_if_fail (field != NULL);
  g_return_if_fail (string_value != NULL);

  s = gst_rtsp_token_writable_structure (token);
  if (s != NULL)
    gst_structure_set (s, field, G_TYPE_STRING, string_value, NULL);
}

/**
 * gst_rtsp_token_set_bool:
 * @token: The #GstRTSPToken.
 * @field: field to set
 * @bool_value: boolean value to set
 *
 * Sets a boolean value on @token.
 *
 * Since: 1.14
 */
void
gst_rtsp_token_set_bool (GstRTSPToken * token, const gchar * field,
    gboolean bool_value)
{
  GstStructure *s;

  g_return_if_fail (token != NULL);
  g_return_if_fail (field != NULL);

  s = gst_rtsp_token_writable_structure (token);
  if (s != NULL)
    gst_structure_set (s, field, G_TYPE_BOOLEAN, bool_value, NULL);
}

/**
 * gst_rtsp_token_get_structure:
 * @token: The #GstRTSPToken.
 *
 * Access the structure of the token.
 *
 * Returns: (transfer none): The structure of the token. The structure is still
 * owned by the token, which means that you should not free it and that the
 * pointer becomes invalid when you free the token.
 *
 * MT safe.
 */
const GstStructure *
gst_rtsp_token_get_structure (GstRTSPToken * token)
{
  g_return_val_if_fail (GST_IS_RTSP_TOKEN (token), NULL);

  return GST_RTSP_TOKEN_STRUCTURE (token);
}

/**
 * gst_rtsp_token_writable_structure:
 * @token: The #GstRTSPToken.
 *
 * Get a writable version of the structure.
 *
 * Returns: (transfer none): The structure of the token. The structure is still
 * owned by the token, which means that you should not free it and that the
 * pointer becomes invalid when you free the token. This function checks if
 * @token is writable and will never return %NULL.
 *
 * MT safe.
 */
GstStructure *
gst_rtsp_token_writable_structure (GstRTSPToken * token)
{
  g_return_val_if_fail (GST_IS_RTSP_TOKEN (token), NULL);
  g_return_val_if_fail (gst_mini_object_is_writable (GST_MINI_OBJECT_CAST
          (token)), NULL);

  return GST_RTSP_TOKEN_STRUCTURE (token);
}

/**
 * gst_rtsp_token_get_string:
 * @token: a #GstRTSPToken
 * @field: a field name
 *
 * Get the string value of @field in @token.
 *
 * Returns: (transfer none) (nullable): the string value of @field in
 * @token or %NULL when @field is not defined in @token. The string
 * becomes invalid when you free @token.
 */
const gchar *
gst_rtsp_token_get_string (GstRTSPToken * token, const gchar * field)
{
  return gst_structure_get_string (GST_RTSP_TOKEN_STRUCTURE (token), field);
}

/**
 * gst_rtsp_token_is_allowed:
 * @token: a #GstRTSPToken
 * @field: a field name
 *
 * Check if @token has a boolean @field and if it is set to %TRUE.
 *
 * Returns: %TRUE if @token has a boolean field named @field set to %TRUE.
 */
gboolean
gst_rtsp_token_is_allowed (GstRTSPToken * token, const gchar * field)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_TOKEN (token), FALSE);
  g_return_val_if_fail (field != NULL, FALSE);

  if (!gst_structure_get_boolean (GST_RTSP_TOKEN_STRUCTURE (token), field,
          &result))
    result = FALSE;

  return result;
}
