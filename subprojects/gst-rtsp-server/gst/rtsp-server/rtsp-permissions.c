/* GStreamer
 * Copyright (C) 2013 Wim Taymans <wim.taymans at gmail.com>
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
 * SECTION:rtsp-permissions
 * @short_description: Roles and associated permissions
 * @see_also: #GstRTSPToken, #GstRTSPAuth
 *
 * The #GstRTSPPermissions object contains an array of roles and associated
 * permissions. The roles are represented with a string and the permissions with
 * a generic #GstStructure.
 *
 * The permissions are deliberately kept generic. The possible values of the
 * roles and #GstStructure keys and values are only determined by the #GstRTSPAuth
 * object that performs the checks on the permissions and the current
 * #GstRTSPToken.
 *
 * As a convenience function, gst_rtsp_permissions_is_allowed() can be used to
 * check if the permissions contains a role that contains the boolean value
 * %TRUE for the the given key.
 *
 * Last reviewed on 2013-07-15 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-permissions.h"

typedef struct _GstRTSPPermissionsImpl
{
  GstRTSPPermissions permissions;

  /* Roles, array of GstStructure */
  GPtrArray *roles;
} GstRTSPPermissionsImpl;

static void
free_structure (GstStructure * structure)
{
  gst_structure_set_parent_refcount (structure, NULL);
  gst_structure_free (structure);
}

//GST_DEBUG_CATEGORY_STATIC (rtsp_permissions_debug);
//#define GST_CAT_DEFAULT rtsp_permissions_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstRTSPPermissions, gst_rtsp_permissions);

static void gst_rtsp_permissions_init (GstRTSPPermissionsImpl * permissions);

static void
_gst_rtsp_permissions_free (GstRTSPPermissions * permissions)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;

  g_ptr_array_free (impl->roles, TRUE);

  g_slice_free1 (sizeof (GstRTSPPermissionsImpl), permissions);
}

static GstRTSPPermissions *
_gst_rtsp_permissions_copy (GstRTSPPermissionsImpl * permissions)
{
  GstRTSPPermissionsImpl *copy;
  guint i;

  copy = (GstRTSPPermissionsImpl *) gst_rtsp_permissions_new ();

  for (i = 0; i < permissions->roles->len; i++) {
    GstStructure *entry = g_ptr_array_index (permissions->roles, i);
    GstStructure *entry_copy = gst_structure_copy (entry);

    gst_structure_set_parent_refcount (entry_copy,
        &copy->permissions.mini_object.refcount);
    g_ptr_array_add (copy->roles, entry_copy);
  }

  return GST_RTSP_PERMISSIONS (copy);
}

static void
gst_rtsp_permissions_init (GstRTSPPermissionsImpl * permissions)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (permissions), 0,
      GST_TYPE_RTSP_PERMISSIONS,
      (GstMiniObjectCopyFunction) _gst_rtsp_permissions_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_rtsp_permissions_free);

  permissions->roles =
      g_ptr_array_new_with_free_func ((GDestroyNotify) free_structure);
}

static void
add_role_from_structure (GstRTSPPermissionsImpl * impl,
    GstStructure * structure)
{
  guint i, len;
  const gchar *role = gst_structure_get_name (structure);

  len = impl->roles->len;
  for (i = 0; i < len; i++) {
    GstStructure *entry = g_ptr_array_index (impl->roles, i);

    if (gst_structure_has_name (entry, role)) {
      g_ptr_array_remove_index_fast (impl->roles, i);
      break;
    }
  }

  gst_structure_set_parent_refcount (structure,
      &impl->permissions.mini_object.refcount);
  g_ptr_array_add (impl->roles, structure);
}

/**
 * gst_rtsp_permissions_new:
 *
 * Create a new empty Authorization permissions.
 *
 * Returns: (transfer full): a new empty authorization permissions.
 */
GstRTSPPermissions *
gst_rtsp_permissions_new (void)
{
  GstRTSPPermissionsImpl *permissions;

  permissions = g_slice_new0 (GstRTSPPermissionsImpl);
  gst_rtsp_permissions_init (permissions);

  return GST_RTSP_PERMISSIONS (permissions);
}

/**
 * gst_rtsp_permissions_add_permission_for_role:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 * @permission: the permission
 * @allowed: whether the role has this permission or not
 *
 * Add a new @permission for @role to @permissions with the access in @allowed.
 *
 * Since: 1.14
 */
void
gst_rtsp_permissions_add_permission_for_role (GstRTSPPermissions * permissions,
    const gchar * role, const gchar * permission, gboolean allowed)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;
  guint i, len;

  g_return_if_fail (GST_IS_RTSP_PERMISSIONS (permissions));
  g_return_if_fail (gst_mini_object_is_writable (&permissions->mini_object));
  g_return_if_fail (role != NULL);
  g_return_if_fail (permission != NULL);

  len = impl->roles->len;
  for (i = 0; i < len; i++) {
    GstStructure *entry = g_ptr_array_index (impl->roles, i);

    if (gst_structure_has_name (entry, role)) {
      gst_structure_set (entry, permission, G_TYPE_BOOLEAN, allowed, NULL);
      return;
    }
  }

  gst_rtsp_permissions_add_role (permissions, role,
      permission, G_TYPE_BOOLEAN, allowed, NULL);
}

/**
 * gst_rtsp_permissions_add_role_empty: (rename-to gst_rtsp_permissions_add_role)
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 *
 * Add a new @role to @permissions without any permissions. You can add
 * permissions for the role with gst_rtsp_permissions_add_permission_for_role().
 *
 * Since: 1.14
 */
void
gst_rtsp_permissions_add_role_empty (GstRTSPPermissions * permissions,
    const gchar * role)
{
  gst_rtsp_permissions_add_role (permissions, role, NULL);
}

/**
 * gst_rtsp_permissions_add_role:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 * @fieldname: the first field name
 * @...: additional arguments
 *
 * Add a new @role to @permissions with the given variables. The fields
 * are the same layout as gst_structure_new().
 */
void
gst_rtsp_permissions_add_role (GstRTSPPermissions * permissions,
    const gchar * role, const gchar * fieldname, ...)
{
  va_list var_args;

  va_start (var_args, fieldname);
  gst_rtsp_permissions_add_role_valist (permissions, role, fieldname, var_args);
  va_end (var_args);
}

/**
 * gst_rtsp_permissions_add_role_valist:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 * @fieldname: the first field name
 * @var_args: additional fields to add
 *
 * Add a new @role to @permissions with the given variables. Structure fields
 * are set according to the varargs in a manner similar to gst_structure_new().
 */
void
gst_rtsp_permissions_add_role_valist (GstRTSPPermissions * permissions,
    const gchar * role, const gchar * fieldname, va_list var_args)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;
  GstStructure *structure;

  g_return_if_fail (GST_IS_RTSP_PERMISSIONS (permissions));
  g_return_if_fail (gst_mini_object_is_writable (&permissions->mini_object));
  g_return_if_fail (role != NULL);

  structure = gst_structure_new_valist (role, fieldname, var_args);
  g_return_if_fail (structure != NULL);

  add_role_from_structure (impl, structure);
}

/**
 * gst_rtsp_permissions_add_role_from_structure:
 *
 * Add a new role to @permissions based on @structure, for example
 * given a role named `tester`, which should be granted a permission named
 * `permission1`, the structure could be created with:
 *
 * ```
 * gst_structure_new ("tester", "permission1", G_TYPE_BOOLEAN, TRUE, NULL);
 * ```
 *
 * Since: 1.14
 */
void
gst_rtsp_permissions_add_role_from_structure (GstRTSPPermissions * permissions,
    GstStructure * structure)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;
  GstStructure *copy;

  g_return_if_fail (GST_IS_RTSP_PERMISSIONS (permissions));
  g_return_if_fail (GST_IS_STRUCTURE (structure));

  copy = gst_structure_copy (structure);

  add_role_from_structure (impl, copy);
}

/**
 * gst_rtsp_permissions_remove_role:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 *
 * Remove all permissions for @role in @permissions.
 */
void
gst_rtsp_permissions_remove_role (GstRTSPPermissions * permissions,
    const gchar * role)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;
  guint i, len;

  g_return_if_fail (GST_IS_RTSP_PERMISSIONS (permissions));
  g_return_if_fail (gst_mini_object_is_writable (&permissions->mini_object));
  g_return_if_fail (role != NULL);

  len = impl->roles->len;
  for (i = 0; i < len; i++) {
    GstStructure *entry = g_ptr_array_index (impl->roles, i);

    if (gst_structure_has_name (entry, role)) {
      g_ptr_array_remove_index_fast (impl->roles, i);
      break;
    }
  }
}

/**
 * gst_rtsp_permissions_get_role:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 *
 * Get all permissions for @role in @permissions.
 *
 * Returns: (transfer none): the structure with permissions for @role. It
 * remains valid for as long as @permissions is valid.
 */
const GstStructure *
gst_rtsp_permissions_get_role (GstRTSPPermissions * permissions,
    const gchar * role)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;
  guint i, len;

  g_return_val_if_fail (GST_IS_RTSP_PERMISSIONS (permissions), NULL);
  g_return_val_if_fail (role != NULL, NULL);

  len = impl->roles->len;
  for (i = 0; i < len; i++) {
    GstStructure *entry = g_ptr_array_index (impl->roles, i);

    if (gst_structure_has_name (entry, role))
      return entry;
  }
  return NULL;
}

/**
 * gst_rtsp_permissions_is_allowed:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 * @permission: a permission
 *
 * Check if @role in @permissions is given permission for @permission.
 *
 * Returns: %TRUE if @role is allowed @permission.
 */
gboolean
gst_rtsp_permissions_is_allowed (GstRTSPPermissions * permissions,
    const gchar * role, const gchar * permission)
{
  const GstStructure *str;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_PERMISSIONS (permissions), FALSE);
  g_return_val_if_fail (role != NULL, FALSE);
  g_return_val_if_fail (permission != NULL, FALSE);

  str = gst_rtsp_permissions_get_role (permissions, role);
  if (str == NULL)
    return FALSE;

  if (!gst_structure_get_boolean (str, permission, &result))
    result = FALSE;

  return result;
}
