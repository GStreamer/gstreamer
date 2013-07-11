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

#include <string.h>

#include "rtsp-permissions.h"

typedef struct _GstRTSPPermissionsImpl
{
  GstRTSPPermissions permissions;

  /* Roles, array of RoleEntry */
  GArray *roles;
} GstRTSPPermissionsImpl;

typedef struct
{
  gchar *role;
  GstStructure *structure;
} RoleEntry;

static void
clear_role_entry (RoleEntry * role)
{
  g_free (role->role);
  gst_structure_set_parent_refcount (role->structure, NULL);
  gst_structure_free (role->structure);
}

//GST_DEBUG_CATEGORY_STATIC (rtsp_permissions_debug);
//#define GST_CAT_DEFAULT rtsp_permissions_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstRTSPPermissions, gst_rtsp_permissions);

static void gst_rtsp_permissions_init (GstRTSPPermissionsImpl * permissions,
    GstStructure * structure);

static void
_gst_rtsp_permissions_free (GstRTSPPermissions * permissions)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;

  g_array_free (impl->roles, TRUE);

  g_slice_free1 (sizeof (GstRTSPPermissionsImpl), permissions);
}

static GstRTSPPermissions *
_gst_rtsp_permissions_copy (GstRTSPPermissionsImpl * permissions)
{
  GstRTSPPermissionsImpl *copy;
  GstStructure *structure;

  copy = g_slice_new0 (GstRTSPPermissionsImpl);
  gst_rtsp_permissions_init (copy, structure);

  return GST_RTSP_PERMISSIONS (copy);
}

static void
gst_rtsp_permissions_init (GstRTSPPermissionsImpl * permissions,
    GstStructure * structure)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (permissions), 0,
      GST_TYPE_RTSP_PERMISSIONS,
      (GstMiniObjectCopyFunction) _gst_rtsp_permissions_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_rtsp_permissions_free);

  permissions->roles = g_array_new (FALSE, TRUE, sizeof (RoleEntry));
  g_array_set_clear_func (permissions->roles,
      (GDestroyNotify) clear_role_entry);
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

  gst_rtsp_permissions_init (permissions,
      gst_structure_new_empty ("GstRTSPPermissions"));

  return GST_RTSP_PERMISSIONS (permissions);
}

/**
 * gst_rtsp_permissions_add_role:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 * @structure: the permissions structure
 *
 * Add the configuration in @structure to @permissions for @role.
 */
void
gst_rtsp_permissions_add_role (GstRTSPPermissions * permissions,
    const gchar * role, GstStructure * structure)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;
  gint i, len;
  RoleEntry item;
  gboolean found;

  g_return_if_fail (GST_IS_RTSP_PERMISSIONS (permissions));
  g_return_if_fail (gst_mini_object_is_writable (&permissions->mini_object));
  g_return_if_fail (role != NULL);
  g_return_if_fail (structure != NULL);

  len = impl->roles->len;
  found = FALSE;
  for (i = 0; i < len; i++) {
    RoleEntry *entry = &g_array_index (impl->roles, RoleEntry, i);

    if (g_str_equal (entry->role, role)) {
      gst_structure_free (entry->structure);
      entry->structure = structure;
      found = TRUE;
      break;
    }
  }
  if (!found) {
    item.role = g_strdup (role);
    item.structure = structure;
    gst_structure_set_parent_refcount (structure,
        &impl->permissions.mini_object.refcount);
    g_array_append_val (impl->roles, item);
  }
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
  g_return_if_fail (GST_IS_RTSP_PERMISSIONS (permissions));
  g_return_if_fail (gst_mini_object_is_writable (&permissions->mini_object));
  g_return_if_fail (role != NULL);
}

/**
 * gst_rtsp_permissions_get_role:
 * @permissions: a #GstRTSPPermissions
 * @role: a role
 *
 * Get all permissions for @role in @permissions.
 *
 * Returns: the structure with permissions for @role.
 */
const GstStructure *
gst_rtsp_permissions_get_role (GstRTSPPermissions * permissions,
    const gchar * role)
{
  GstRTSPPermissionsImpl *impl = (GstRTSPPermissionsImpl *) permissions;
  gint i, len;

  g_return_val_if_fail (GST_IS_RTSP_PERMISSIONS (permissions), NULL);
  g_return_val_if_fail (role != NULL, NULL);

  len = impl->roles->len;
  for (i = 0; i < len; i++) {
    RoleEntry *entry = &g_array_index (impl->roles, RoleEntry, i);

    if (g_str_equal (entry->role, role))
      return entry->structure;
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
