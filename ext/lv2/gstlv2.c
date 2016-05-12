/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 *               2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *               2003 Andy Wingo <wingo at pobox.com>
 *               2016 Thibault Saunier <thibault.saunier@collabora.com>
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
 * SECTION:element-lv2
 * @short_description: bridge for LV2.
 *
 * LV2 is a standard for plugins and matching host applications,
 * mainly targeted at audio processing and generation.  It is intended as
 * a successor to LADSPA (Linux Audio Developer's Simple Plugin API).
 *
 * The LV2 element is a bridge for plugins using the
 * <ulink url="http://www.lv2plug.in/">LV2</ulink> API.  It scans all
 * installed LV2 plugins and registers them as gstreamer elements.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstlv2.h"

#include <gst/audio/audio-channels.h>
#include <lv2/lv2plug.in/ns/ext/port-groups/port-groups.h>

GST_DEBUG_CATEGORY (lv2_debug);
#define GST_CAT_DEFAULT lv2_debug

#define GST_LV2_DEFAULT_PATH \
  "/usr/lib/lv2" G_SEARCHPATH_SEPARATOR_S \
  "/usr/local/lib/lv2" G_SEARCHPATH_SEPARATOR_S \
  LIBDIR "/lv2"

/* search the plugin path
 */
static gboolean
lv2_plugin_discover (GstPlugin * plugin)
{
  guint j, num_sink_pads, num_src_pads;
  LilvIter *i;
  const LilvPlugins *plugins = lilv_world_get_all_plugins (world);

  for (i = lilv_plugins_begin (plugins); !lilv_plugins_is_end (plugins, i);
      i = lilv_plugins_next (plugins, i)) {
    const LilvPlugin *lv2plugin = lilv_plugins_get (plugins, i);
    const gchar *plugin_uri, *p;
    LilvNodes *required_features;
    gchar *type_name;
    GHashTable *port_groups = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, NULL);

    plugin_uri = lilv_node_as_uri (lilv_plugin_get_uri (lv2plugin));
    /* construct the type name from plugin URI */
    if ((p = strstr (plugin_uri, "://"))) {
      /* cut off the protocol (e.g. http://) */
      type_name = g_strdup (&p[3]);
    } else {
      type_name = g_strdup (plugin_uri);
    }
    g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-+", '-');

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name))
      goto next;

    /* check if we support the required host features */
    required_features = lilv_plugin_get_required_features (lv2plugin);
    if (required_features) {
      GST_FIXME ("lv2 plugin %s needs host features", plugin_uri);
      // TODO: implement host features, will be passed to
      // lilv_plugin_instantiate()
      goto next;
    }

    /* check if this has any audio ports */
    num_sink_pads = num_src_pads = 0;
    for (j = 0; j < lilv_plugin_get_num_ports (lv2plugin); j++) {
      const LilvPort *port = lilv_plugin_get_port_by_index (lv2plugin, j);

      if (lilv_port_is_a (lv2plugin, port, audio_class)) {
        const gboolean is_input = lilv_port_is_a (lv2plugin, port, input_class);
        LilvNodes *lv2group = lilv_port_get (lv2plugin, port, group_pred);

        if (lv2group) {
          const gchar *uri = lilv_node_as_uri (lv2group);

          if (g_hash_table_contains (port_groups, uri))
            continue;

          g_hash_table_add (port_groups, g_strdup (uri));
          lilv_node_free (lv2group);
        }

        if (is_input)
          num_sink_pads++;
        else
          num_src_pads++;
      }
    }
    if (num_sink_pads == 0 && num_src_pads == 0) {
      GST_FIXME ("plugin %s has no pads", type_name);
    } else if (num_sink_pads == 0) {
      if (num_src_pads != 1) {
        GST_FIXME ("plugin %s is not a GstBaseSrc (num_src_pads: %d)",
            type_name, num_src_pads);
        goto next;
      }
      gst_lv2_source_register_element (plugin, type_name, (gpointer) lv2plugin);
    } else if (num_src_pads == 0) {
      GST_FIXME ("plugin %s is a sink element (num_sink_pads: %d"
          " num_src_pads: %d)", type_name, num_sink_pads, num_src_pads);
    } else {
      if (num_sink_pads != 1 || num_src_pads != 1) {
        GST_FIXME ("plugin %s is not a GstAudioFilter (num_sink_pads: %d"
            " num_src_pads: %d)", type_name, num_sink_pads, num_src_pads);
        goto next;
      }
      gst_lv2_filter_register_element (plugin, type_name, (gpointer) lv2plugin);
    }

  next:
    g_free (type_name);
    g_hash_table_unref (port_groups);
  }

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (lv2_debug, "lv2",
      GST_DEBUG_FG_GREEN | GST_DEBUG_BG_BLACK | GST_DEBUG_BOLD, "LV2");

  world = lilv_world_new ();
  lilv_world_load_all (world);

  audio_class = lilv_new_uri (world, LILV_URI_AUDIO_PORT);
  control_class = lilv_new_uri (world, LILV_URI_CONTROL_PORT);
  input_class = lilv_new_uri (world, LILV_URI_INPUT_PORT);
  output_class = lilv_new_uri (world, LILV_URI_OUTPUT_PORT);

#define NS_LV2 "http://lv2plug.in/ns/lv2core#"
#define NS_PG  "http://lv2plug.in/ns/ext/port-groups#"

  integer_prop = lilv_new_uri (world, NS_LV2 "integer");
  toggled_prop = lilv_new_uri (world, NS_LV2 "toggled");
  in_place_broken_pred = lilv_new_uri (world, NS_LV2 "inPlaceBroken");
  group_pred = lilv_new_uri (world, LV2_PORT_GROUPS__group);
  has_role_pred = lilv_new_uri (world, NS_PG "role");

  /* FIXME Verify what should be used here */
  lv2_symbol_pred = lilv_new_uri (world, LILV_NS_LV2 "symbol");

  center_role = lilv_new_uri (world, LV2_PORT_GROUPS__center);
  left_role = lilv_new_uri (world, LV2_PORT_GROUPS__left);
  right_role = lilv_new_uri (world, LV2_PORT_GROUPS__right);
  rear_center_role = lilv_new_uri (world, LV2_PORT_GROUPS__rearCenter);
  rear_left_role = lilv_new_uri (world, LV2_PORT_GROUPS__rearLeft);
  rear_right_role = lilv_new_uri (world, LV2_PORT_GROUPS__rearLeft);
  lfe_role = lilv_new_uri (world, LV2_PORT_GROUPS__lowFrequencyEffects);
  center_left_role = lilv_new_uri (world, LV2_PORT_GROUPS__centerLeft);
  center_right_role = lilv_new_uri (world, LV2_PORT_GROUPS__centerRight);
  side_left_role = lilv_new_uri (world, LV2_PORT_GROUPS__sideLeft);
  side_right_role = lilv_new_uri (world, LV2_PORT_GROUPS__sideRight);

  gst_plugin_add_dependency_simple (plugin,
      "LV2_PATH", GST_LV2_DEFAULT_PATH, NULL, GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  descriptor_quark = g_quark_from_static_string ("lilv-plugin");

  /* ensure GstAudioChannelPosition type is registered */
  if (!gst_audio_channel_position_get_type ())
    return FALSE;

  if (!lv2_plugin_discover (plugin)) {
    GST_WARNING ("no lv2 plugins found, check LV2_PATH");
  }



  /* we don't want to fail, even if there are no elements registered */
  return TRUE;
}

#ifdef __GNUC__
__attribute__ ((destructor))
#endif
     static void plugin_cleanup (GstPlugin * plugin)
{
  lilv_node_free (audio_class);
  lilv_node_free (control_class);
  lilv_node_free (input_class);
  lilv_node_free (output_class);

  lilv_node_free (integer_prop);
  lilv_node_free (toggled_prop);
  lilv_node_free (in_place_broken_pred);
  lilv_node_free (group_pred);
  lilv_node_free (has_role_pred);
  lilv_node_free (lv2_symbol_pred);

  lilv_node_free (center_role);
  lilv_node_free (left_role);
  lilv_node_free (right_role);
  lilv_node_free (rear_center_role);
  lilv_node_free (rear_left_role);
  lilv_node_free (rear_right_role);
  lilv_node_free (lfe_role);
  lilv_node_free (center_left_role);
  lilv_node_free (center_right_role);
  lilv_node_free (side_left_role);
  lilv_node_free (side_right_role);

  lilv_world_free (world);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    lv2,
    "All LV2 plugins",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
