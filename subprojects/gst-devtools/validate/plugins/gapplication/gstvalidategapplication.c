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

#include "../../gst/validate/validate.h"
#include "../../gst/validate/gst-validate-scenario.h"
#include "../../gst/validate/gst-validate-utils.h"


static gboolean
_execute_stop (GstValidateScenario * scenario, GstValidateAction * action)
{
  g_application_quit (g_application_get_default ());

  return TRUE;
}

static gboolean
gst_validate_gapplication_init (GstPlugin * plugin)
{
  GList *config, *tmp;
  const gchar *appname;

  config = gst_validate_plugin_get_config (plugin);

  if (!config)
    return TRUE;

  for (tmp = config; tmp; tmp = tmp->next) {
    appname = gst_structure_get_string (tmp->data, "application-name");
  }

  if (appname && g_strcmp0 (g_get_prgname (), appname)) {
    GST_INFO_OBJECT (plugin, "App: %s is not %s", g_get_prgname (), appname);
    return TRUE;
  }

  gst_validate_register_action_type_dynamic (plugin, "stop",
      GST_RANK_PRIMARY, _execute_stop, NULL,
      "Sets the pipeline state to NULL",
      GST_VALIDATE_ACTION_TYPE_NO_EXECUTION_NOT_FATAL |
      GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    validategapplication,
    "GstValidate plugin to run validate on gapplication",
    gst_validate_gapplication_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
