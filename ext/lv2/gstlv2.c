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
 * @title: lv2
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
#include "lv2/lv2plug.in/ns/ext/event/event.h"
#include "lv2/lv2plug.in/ns/ext/presets/presets.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"

GST_DEBUG_CATEGORY (lv2_debug);
#define GST_CAT_DEFAULT lv2_debug

#if defined (G_OS_WIN32)
#define GST_LV2_ENVVARS "APPDATA/LV2:COMMONPROGRAMFILES/LV2"
#define GST_LV2_DEFAULT_PATH NULL
#elif defined (HAVE_OSX)
#define GST_LV2_ENVVARS "HOME/Library/Audio/Plug-Ins/LV2:HOME/.lv2"
#define GST_LV2_DEFAULT_PATH \
  "/usr/local/lib/lv2:/usr/lib/lv2:/Library/Audio/Plug-Ins/LV2"
#elif defined (G_OS_UNIX)
#define GST_LV2_ENVVARS "HOME/.lv2"
#define GST_LV2_DEFAULT_PATH \
  "/usr/lib/lv2:" \
  "/usr/lib64/lv2:" \
  "/usr/local/lib/lv2:" \
  "/usr/local/lib64/lv2:" \
  LIBDIR "/lv2"
#else
#error "Unsupported OS"
#endif

GstStructure *lv2_meta_all = NULL;

static void
lv2_plugin_register_element (GstPlugin * plugin, GstStructure * lv2_meta)
{
  guint audio_in, audio_out;

  gst_structure_get_uint (lv2_meta, "audio-in", &audio_in);
  gst_structure_get_uint (lv2_meta, "audio-out", &audio_out);

  if (audio_in == 0) {
    gst_lv2_source_register_element (plugin, lv2_meta);
  } else {
    gst_lv2_filter_register_element (plugin, lv2_meta);
  }
}

static void
lv2_count_ports (const LilvPlugin * lv2plugin, guint * audio_in,
    guint * audio_out, guint * control)
{
  GHashTable *port_groups = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
  guint i;

  *audio_in = *audio_out = *control = 0;
  for (i = 0; i < lilv_plugin_get_num_ports (lv2plugin); i++) {
    const LilvPort *port = lilv_plugin_get_port_by_index (lv2plugin, i);

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
        (*audio_in)++;
      else
        (*audio_out)++;
    } else if (lilv_port_is_a (lv2plugin, port, control_class) ||
        lilv_port_is_a (lv2plugin, port, cv_class)) {
      (*control)++;
    }
  }
  g_hash_table_unref (port_groups);
}

/* search the plugin path */
static gboolean
lv2_plugin_discover (GstPlugin * plugin)
{
  guint audio_in, audio_out, control;
  LilvIter *i;
  const LilvPlugins *plugins = lilv_world_get_all_plugins (world);

  for (i = lilv_plugins_begin (plugins); !lilv_plugins_is_end (plugins, i);
      i = lilv_plugins_next (plugins, i)) {
    GstStructure *lv2_meta = NULL;
    GValue value = { 0, };
    const LilvPlugin *lv2plugin = lilv_plugins_get (plugins, i);
    const gchar *plugin_uri, *p;
    gchar *type_name;
    gboolean can_do_presets;

    plugin_uri = lilv_node_as_uri (lilv_plugin_get_uri (lv2plugin));

    /* check if we support the required host features */
    if (!gst_lv2_check_required_features (lv2plugin)) {
      GST_FIXME ("lv2 plugin %s needs host features", plugin_uri);
      continue;
    }

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

    /* check if this has any audio ports */
    lv2_count_ports (lv2plugin, &audio_in, &audio_out, &control);

    if (audio_in == 0 && audio_out == 0) {
      GST_FIXME ("plugin %s has no audio pads", type_name);
      goto next;
    } else if (audio_in == 0) {
      if (audio_out != 1) {
        GST_FIXME ("plugin %s is not a GstBaseSrc (num_src_pads: %d)",
            type_name, audio_out);
        goto next;
      }
    } else if (audio_out == 0) {
      GST_FIXME ("plugin %s is a sink element (num_sink_pads: %d"
          " num_src_pads: %d)", type_name, audio_in, audio_out);
      goto next;
    } else {
      if (audio_in != 1 || audio_out != 1) {
        GST_FIXME ("plugin %s is not a GstAudioFilter (num_sink_pads: %d"
            " num_src_pads: %d)", type_name, audio_in, audio_out);
        goto next;
      }
    }

    /* check supported extensions */
    can_do_presets = lilv_plugin_has_extension_data (lv2plugin, state_iface)
        || lilv_plugin_has_feature (lv2plugin, state_uri)
        || (control > 0);
    GST_INFO ("plugin %s can%s do presets", type_name,
        (can_do_presets ? "" : "'t"));

    lv2_meta = gst_structure_new ("lv2",
        "element-uri", G_TYPE_STRING, plugin_uri,
        "element-type-name", G_TYPE_STRING, type_name,
        "audio-in", G_TYPE_UINT, audio_in,
        "audio-out", G_TYPE_UINT, audio_out,
        "can-do-presets", G_TYPE_BOOLEAN, can_do_presets, NULL);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_set_boxed (&value, lv2_meta);
    gst_structure_set_value (lv2_meta_all, type_name, &value);
    g_value_unset (&value);

    // don't free type_name
    continue;

  next:
    g_free (type_name);
  }

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res = FALSE;
  gint n = 0;

  GST_DEBUG_CATEGORY_INIT (lv2_debug, "lv2",
      GST_DEBUG_FG_GREEN | GST_DEBUG_BG_BLACK | GST_DEBUG_BOLD, "LV2");

  world = lilv_world_new ();
  lilv_world_load_all (world);
  gst_lv2_host_init ();

/* have been added after lilv-0.22.0, which is the last release */
#ifndef LILV_URI_ATOM_PORT
#define LILV_URI_ATOM_PORT    "http://lv2plug.in/ns/ext/atom#AtomPort"
#endif
#ifndef LILV_URI_CV_PORT
#define LILV_URI_CV_PORT      "http://lv2plug.in/ns/lv2core#CVPort"
#endif

  atom_class = lilv_new_uri (world, LILV_URI_ATOM_PORT);
  audio_class = lilv_new_uri (world, LILV_URI_AUDIO_PORT);
  control_class = lilv_new_uri (world, LILV_URI_CONTROL_PORT);
  cv_class = lilv_new_uri (world, LILV_URI_CV_PORT);
  event_class = lilv_new_uri (world, LILV_URI_EVENT_PORT);
  input_class = lilv_new_uri (world, LILV_URI_INPUT_PORT);
  output_class = lilv_new_uri (world, LILV_URI_OUTPUT_PORT);
  preset_class = lilv_new_uri (world, LV2_PRESETS__Preset);
  state_iface = lilv_new_uri (world, LV2_STATE__interface);
  state_uri = lilv_new_uri (world, LV2_STATE_URI);

  integer_prop = lilv_new_uri (world, LV2_CORE__integer);
  toggled_prop = lilv_new_uri (world, LV2_CORE__toggled);
  designation_pred = lilv_new_uri (world, LV2_CORE__designation);
  in_place_broken_pred = lilv_new_uri (world, LV2_CORE__inPlaceBroken);
  optional_pred = lilv_new_uri (world, LV2_CORE__optionalFeature);
  group_pred = lilv_new_uri (world, LV2_PORT_GROUPS__group);
  supports_event_pred = lilv_new_uri (world, LV2_EVENT__supportsEvent);
  label_pred = lilv_new_uri (world, LILV_NS_RDFS "label");

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
      "LV2_PATH:" GST_LV2_ENVVARS, GST_LV2_DEFAULT_PATH, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_RECURSE);

  /* ensure GstAudioChannelPosition type is registered */
  if (!gst_audio_channel_position_get_type ())
    return FALSE;

  lv2_meta_all = (GstStructure *) gst_plugin_get_cache_data (plugin);
  if (lv2_meta_all) {
    n = gst_structure_n_fields (lv2_meta_all);
  }
  GST_INFO_OBJECT (plugin, "%d entries in cache", n);
  if (!n) {
    lv2_meta_all = gst_structure_new_empty ("lv2");
    if ((res = lv2_plugin_discover (plugin))) {
      n = gst_structure_n_fields (lv2_meta_all);
      GST_INFO_OBJECT (plugin, "%d entries after scanning", n);
      gst_plugin_set_cache_data (plugin, lv2_meta_all);
    }
  } else {
    res = TRUE;
  }

  if (n) {
    gint i;
    const gchar *name;
    const GValue *value;

    GST_INFO_OBJECT (plugin, "register types");

    for (i = 0; i < n; i++) {
      name = gst_structure_nth_field_name (lv2_meta_all, i);
      value = gst_structure_get_value (lv2_meta_all, name);
      if (G_VALUE_TYPE (value) == GST_TYPE_STRUCTURE) {
        GstStructure *lv2_meta = g_value_get_boxed (value);

        lv2_plugin_register_element (plugin, lv2_meta);
      }
    }
  }

  if (!res) {
    GST_WARNING_OBJECT (plugin, "no lv2 plugins found, check LV2_PATH");
  }

  /* we don't want to fail, even if there are no elements registered */
  return TRUE;
}

#ifdef __GNUC__
__attribute__ ((destructor))
#endif
     static void plugin_cleanup (GstPlugin * plugin)
{
  lilv_node_free (atom_class);
  lilv_node_free (audio_class);
  lilv_node_free (control_class);
  lilv_node_free (cv_class);
  lilv_node_free (event_class);
  lilv_node_free (input_class);
  lilv_node_free (output_class);
  lilv_node_free (preset_class);
  lilv_node_free (state_iface);
  lilv_node_free (state_uri);

  lilv_node_free (integer_prop);
  lilv_node_free (toggled_prop);
  lilv_node_free (designation_pred);
  lilv_node_free (in_place_broken_pred);
  lilv_node_free (optional_pred);
  lilv_node_free (group_pred);
  lilv_node_free (supports_event_pred);
  lilv_node_free (label_pred);

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
