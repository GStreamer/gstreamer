/*
 * Copyright (C) 2012 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gmodule.h>

#ifdef GST_OMX_STRUCT_PACKING
# if GST_OMX_STRUCT_PACKING == 1
#  pragma pack(1)
# elif GST_OMX_STRUCT_PACKING == 2
#  pragma pack(2)
# elif GST_OMX_STRUCT_PACKING == 4
#  pragma pack(4)
# elif GST_OMX_STRUCT_PACKING == 8
#  pragma pack(8)
# else
#  error "Unsupported struct packing value"
# endif
#endif

#include <OMX_Core.h>
#include <OMX_Component.h>

#ifdef GST_OMX_STRUCT_PACKING
#pragma pack()
#endif

gint
main (gint argc, gchar ** argv)
{
  gchar *filename;
  GModule *core_module;
  OMX_ERRORTYPE err;
  OMX_ERRORTYPE (*omx_init) (void);
  OMX_ERRORTYPE (*omx_component_name_enum) (OMX_STRING cComponentName,
      OMX_U32 nNameLength, OMX_U32 nIndex);
  OMX_ERRORTYPE (*omx_get_roles_of_component) (OMX_STRING compName,
      OMX_U32 * pNumRoles, OMX_U8 ** roles);
  guint32 i;

  if (argc != 2) {
    g_printerr ("Usage: %s /path/to/libopenmaxil.so\n", argv[0]);
    return -1;
  }

  filename = argv[1];

  if (!g_path_is_absolute (filename)) {
    g_printerr ("'%s' is not an absolute filename\n", filename);
    return -1;
  }

  /* Hack for the Broadcom OpenMAX IL implementation */
  if (g_str_has_suffix (filename, "vc/lib/libopenmaxil.so")) {
    gchar *bcm_host_filename;
    gchar *bcm_host_path;
    GModule *bcm_host_module;
    void (*bcm_host_init) (void);

    bcm_host_path = g_path_get_dirname (filename);
    bcm_host_filename =
        g_build_filename (bcm_host_path, "libbcm_host.so", NULL);

    bcm_host_module = g_module_open (bcm_host_filename, G_MODULE_BIND_LAZY);

    g_free (bcm_host_filename);
    g_free (bcm_host_path);

    if (!bcm_host_module) {
      g_printerr ("Failed to load 'libbcm_host.so'\n");
      return -1;
    }

    if (!g_module_symbol (bcm_host_module, "bcm_host_init",
            (gpointer *) & bcm_host_init)) {
      g_printerr ("Failed to find 'bcm_host_init' in 'libbcm_host.so'\n");
      return -1;
    }

    bcm_host_init ();
  }

  core_module = g_module_open (filename, G_MODULE_BIND_LAZY);
  if (!core_module) {
    g_printerr ("Failed to load '%s'\n", filename);
    return -1;
  }

  if (!g_module_symbol (core_module, "OMX_Init", (gpointer *) & omx_init)) {
    g_printerr ("Failed to find '%s' in '%s'\n", "OMX_Init", filename);
    return -1;
  }

  if (!g_module_symbol (core_module, "OMX_ComponentNameEnum",
          (gpointer *) & omx_component_name_enum)) {
    g_printerr ("Failed to find '%s' in '%s'\n", "OMX_ComponentNameEnum",
        filename);
    return -1;
  }

  if (!g_module_symbol (core_module, "OMX_GetRolesOfComponent",
          (gpointer *) & omx_get_roles_of_component)) {
    g_printerr ("Failed to find '%s' in '%s'\n", "OMX_GetRolesOfComponent",
        filename);
    return -1;
  }


  if ((err = omx_init ()) != OMX_ErrorNone) {
    g_printerr ("Failed to initialize core: %d\n", err);
    return -1;
  }

  i = 0;
  while (err == OMX_ErrorNone) {
    gchar component_name[1024];

    err = omx_component_name_enum (component_name, sizeof (component_name), i);
    if (err == OMX_ErrorNone || err == OMX_ErrorNoMore) {
      guint32 nroles;

      g_print ("Component %d: %s\n", i, component_name);

      if (omx_get_roles_of_component (component_name, (OMX_U32 *) & nroles,
              NULL) == OMX_ErrorNone && nroles > 0) {
        gchar **roles = g_new (gchar *, nroles);
        gint j;

        roles[0] = g_new0 (gchar, 129 * nroles);
        for (j = 1; j < nroles; j++) {
          roles[j] = roles[j - 1] + 129;
        }

        if (omx_get_roles_of_component (component_name, (OMX_U32 *) & nroles,
                (OMX_U8 **) roles) == OMX_ErrorNone) {
          for (j = 0; j < nroles; j++) {
            g_print ("  Role %d: %s\n", j, roles[j]);
          }
        }
        g_free (roles[0]);
        g_free (roles);
      }
    }
    i++;
  }

  return 0;
}
