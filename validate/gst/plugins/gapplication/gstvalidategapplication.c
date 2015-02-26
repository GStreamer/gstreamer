/* GStreamer
 *
 * Copyright (C) 2015 Raspberry Pi Foundation
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gstvalidategapplication.c: GstValidateAction overrides for gapplication
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gio/gio.h>

#include "../../validate/gst-validate-scenario.h"
#include "../../validate/gst-validate-utils.h"


static gboolean
_execute_stop (GstValidateScenario * scenario, GstValidateAction * action)
{
  g_application_quit (g_application_get_default ());

  return TRUE;
}

static gboolean
gst_validate_gapplication_init (GstPlugin * plugin)
{
  GList *structures, *tmp;
  const gchar *appname = NULL, *config = g_getenv ("GST_VALIDATE_CONFIG");

  if (!config)
    return TRUE;

  structures = gst_validate_utils_structs_parse_from_filename (config);

  if (!structures)
    return TRUE;

  for (tmp = structures; tmp; tmp = tmp->next) {
    if (gst_structure_has_name (tmp->data, "gapplication"))
      appname = gst_structure_get_string (tmp->data, "application-name");
  }
  g_list_free_full (structures, (GDestroyNotify) gst_structure_free);

  if (appname && g_strcmp0 (g_get_prgname (), appname))
    return TRUE;

  gst_validate_register_action_type_dynamic (plugin, "stop",
      GST_RANK_PRIMARY, _execute_stop, NULL,
      "Sets the pipeline state to NULL",
      GST_VALIDATE_ACTION_TYPE_NO_EXECUTION_NOT_FATAL);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gstvalidategapplication,
    "GstValidate plugin to run validate on gapplication",
    gst_validate_gapplication_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
