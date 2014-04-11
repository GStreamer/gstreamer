/*
 * parsechannels.c - 
 * Copyright (C) 2008 Zaheer Abbas Merali
 * 
 * Authors:
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
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

#include <glib.h>
#include <glib-object.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>

#include <gst/gst-i18n-plugin.h>

#include "parsechannels.h"

#include <linux/dvb/frontend.h>

GST_DEBUG_CATEGORY_EXTERN (dvb_base_bin_debug);
#define GST_CAT_DEFAULT dvb_base_bin_debug

/* TODO:
 * Store the channels hash table around instead of constantly parsing it
 * Detect when the file changed on disk
 */

/* this will do zap style channels.conf only for the moment */
static GHashTable *
parse_channels_conf_from_file (GstElement * dvbbasebin, const gchar * filename,
    GError ** error)
{
  gchar *contents;
  gchar **lines;
  gchar *line;
  gchar **fields;
  const gchar *terrestrial[] = { "inversion", "bandwidth",
    "code-rate-hp", "code-rate-lp", "modulation", "transmission-mode",
    "guard", "hierarchy"
  };
  const gchar *satellite[] = { "polarity", "diseqc-source",
    "symbol-rate"
  };
  const gchar *cable[] = { "inversion", "symbol-rate", "code-rate-hp",
    "modulation"
  };
  int i, parsedchannels = 0;
  GHashTable *res;
  GError *err = NULL;

  GST_INFO_OBJECT (dvbbasebin, "parsing '%s'", filename);

  if (!g_file_get_contents (filename, &contents, NULL, &err))
    goto open_fail;

  lines = g_strsplit (contents, "\n", 0);
  res = g_hash_table_new (g_str_hash, g_str_equal);

  i = 0;
  line = lines[0];
  while (line != NULL) {
    if (line[0] != '#') {
      int numfields;
      gboolean parsed = FALSE;
      GHashTable *params = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, g_free);

      fields = g_strsplit (line, ":", 0);
      numfields = g_strv_length (fields);
      if (numfields == 8) {
        /* satellite */
        int j;

        g_hash_table_insert (params, g_strdup ("type"), g_strdup ("satellite"));
        for (j = 2; j <= 4; j++) {
          g_hash_table_insert (params, g_strdup (satellite[j - 2]),
              g_strdup (fields[j]));
        }
        g_hash_table_insert (params, g_strdup ("frequency"),
            g_strdup_printf ("%d", atoi (fields[1]) * 1000));
        parsed = TRUE;
      } else if (numfields == 13) {
        /* terrestrial */
        int j;

        g_hash_table_insert (params, g_strdup ("type"),
            g_strdup ("terrestrial"));
        for (j = 2; j <= 9; j++) {
          g_hash_table_insert (params, g_strdup (terrestrial[j - 2]),
              g_strdup (fields[j]));
        }
        g_hash_table_insert (params, g_strdup ("frequency"),
            g_strdup (fields[1]));
        parsed = TRUE;
      } else if (numfields == 9) {
        /* cable */
        int j;

        g_hash_table_insert (params, g_strdup ("type"), g_strdup ("cable"));
        for (j = 2; j <= 5; j++) {
          g_hash_table_insert (params, g_strdup (cable[j - 2]),
              g_strdup (fields[j]));
        }
        g_hash_table_insert (params, g_strdup ("frequency"),
            g_strdup (fields[1]));
        parsed = TRUE;
      } else if (numfields == 6) {
        /* atsc (vsb/qam) */
        g_hash_table_insert (params, g_strdup ("type"), g_strdup ("atsc"));
        g_hash_table_insert (params, g_strdup ("modulation"),
            g_strdup (fields[2]));

        g_hash_table_insert (params, g_strdup ("frequency"),
            g_strdup (fields[1]));
        parsed = TRUE;
      }
      if (parsed) {
        g_hash_table_insert (params, g_strdup ("sid"),
            g_strdup (fields[numfields - 1]));
        g_hash_table_insert (res, g_strdup (fields[0]), params);
        parsedchannels++;
      }
      g_strfreev (fields);
    }
    line = lines[++i];
  }
  g_strfreev (lines);
  g_free (contents);

  if (parsedchannels == 0)
    goto no_channels;

  return res;

open_fail:
  {
    if (err->code == G_FILE_ERROR_NOENT) {
      g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND,
          _("Couldn't find DVB channel configuration file"));
    } else {
      g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ,
          _("Couldn't load DVB channel configuration file: %s"), err->message);
    }
    g_clear_error (&err);
    return NULL;
  }

no_channels:
  {
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        _("DVB channel configuration file doesn't contain any channels"));
    g_hash_table_unref (res);
    return NULL;
  }
}

static gboolean
remove_channel_from_hash (gpointer key, gpointer value, gpointer user_data)
{
  if (key)
    g_free (key);
  if (value)
    g_hash_table_destroy ((GHashTable *) value);
  return TRUE;
}

static void
destroy_channels_hash (GHashTable * channels)
{
  g_hash_table_foreach_remove (channels, remove_channel_from_hash, NULL);
}

gboolean
set_properties_for_channel (GstElement * dvbbasebin,
    const gchar * channel_name, GError ** error)
{
  gboolean ret = FALSE;
  GHashTable *channels, *params;
  gchar *filename;
  gchar *type;
  const gchar *adapter;

  filename = g_strdup (g_getenv ("GST_DVB_CHANNELS_CONF"));
  if (filename == NULL) {
    filename = g_build_filename (g_get_user_config_dir (),
        "gstreamer-" GST_API_VERSION, "dvb-channels.conf", NULL);
  }
  channels = parse_channels_conf_from_file (dvbbasebin, filename, error);
  g_free (filename);

  if (!channels)
    goto beach;

  params = g_hash_table_lookup (channels, channel_name);

  if (!params)
    goto unknown_channel;

  g_object_set (dvbbasebin, "program-numbers",
      g_hash_table_lookup (params, "sid"), NULL);
  /* check if it is terrestrial or satellite */
  adapter = g_getenv ("GST_DVB_ADAPTER");
  if (adapter)
    g_object_set (dvbbasebin, "adapter", atoi (adapter), NULL);
  g_object_set (dvbbasebin, "frequency",
      atoi (g_hash_table_lookup (params, "frequency")), NULL);
  type = g_hash_table_lookup (params, "type");
  if (strcmp (type, "terrestrial") == 0) {
    gchar *val;

    g_object_set (dvbbasebin, "delsys", SYS_DVBT, NULL);

    val = g_hash_table_lookup (params, "inversion");
    if (strcmp (val, "INVERSION_OFF") == 0)
      g_object_set (dvbbasebin, "inversion", 0, NULL);
    else if (strcmp (val, "INVERSION_ON") == 0)
      g_object_set (dvbbasebin, "inversion", 1, NULL);
    else
      g_object_set (dvbbasebin, "inversion", 2, NULL);

    val = g_hash_table_lookup (params, "bandwidth");
    if (strcmp (val, "BANDWIDTH_8_MHZ") == 0)
      g_object_set (dvbbasebin, "bandwidth", 0, NULL);
    else if (strcmp (val, "BANDWIDTH_7_MHZ") == 0)
      g_object_set (dvbbasebin, "bandwidth", 1, NULL);
    else if (strcmp (val, "BANDWIDTH_6_MHZ") == 0)
      g_object_set (dvbbasebin, "bandwidth", 2, NULL);
    else
      g_object_set (dvbbasebin, "bandwidth", 3, NULL);

    val = g_hash_table_lookup (params, "code-rate-hp");
    if (strcmp (val, "FEC_NONE") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 0, NULL);
    else if (strcmp (val, "FEC_1_2") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 1, NULL);
    else if (strcmp (val, "FEC_2_3") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 2, NULL);
    else if (strcmp (val, "FEC_3_4") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 3, NULL);
    else if (strcmp (val, "FEC_4_5") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 4, NULL);
    else if (strcmp (val, "FEC_5_6") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 5, NULL);
    else if (strcmp (val, "FEC_6_7") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 6, NULL);
    else if (strcmp (val, "FEC_7_8") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 7, NULL);
    else if (strcmp (val, "FEC_8_9") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 8, NULL);
    else
      g_object_set (dvbbasebin, "code-rate-hp", 9, NULL);

    val = g_hash_table_lookup (params, "code-rate-lp");
    if (strcmp (val, "FEC_NONE") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 0, NULL);
    else if (strcmp (val, "FEC_1_2") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 1, NULL);
    else if (strcmp (val, "FEC_2_3") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 2, NULL);
    else if (strcmp (val, "FEC_3_4") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 3, NULL);
    else if (strcmp (val, "FEC_4_5") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 4, NULL);
    else if (strcmp (val, "FEC_5_6") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 5, NULL);
    else if (strcmp (val, "FEC_6_7") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 6, NULL);
    else if (strcmp (val, "FEC_7_8") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 7, NULL);
    else if (strcmp (val, "FEC_8_9") == 0)
      g_object_set (dvbbasebin, "code-rate-lp", 8, NULL);
    else
      g_object_set (dvbbasebin, "code-rate-lp", 9, NULL);

    val = g_hash_table_lookup (params, "modulation");
    if (strcmp (val, "QPSK") == 0)
      g_object_set (dvbbasebin, "modulation", 0, NULL);
    else if (strcmp (val, "QAM_16") == 0)
      g_object_set (dvbbasebin, "modulation", 1, NULL);
    else if (strcmp (val, "QAM_32") == 0)
      g_object_set (dvbbasebin, "modulation", 2, NULL);
    else if (strcmp (val, "QAM_64") == 0)
      g_object_set (dvbbasebin, "modulation", 3, NULL);
    else if (strcmp (val, "QAM_128") == 0)
      g_object_set (dvbbasebin, "modulation", 4, NULL);
    else if (strcmp (val, "QAM_256") == 0)
      g_object_set (dvbbasebin, "modulation", 5, NULL);
    else
      g_object_set (dvbbasebin, "modulation", 6, NULL);

    val = g_hash_table_lookup (params, "transmission-mode");
    if (strcmp (val, "TRANSMISSION_MODE_2K") == 0)
      g_object_set (dvbbasebin, "trans-mode", 0, NULL);
    else if (strcmp (val, "TRANSMISSION_MODE_8K") == 0)
      g_object_set (dvbbasebin, "trans-mode", 1, NULL);
    else
      g_object_set (dvbbasebin, "trans-mode", 2, NULL);

    val = g_hash_table_lookup (params, "guard");
    if (strcmp (val, "GUARD_INTERVAL_1_32") == 0)
      g_object_set (dvbbasebin, "guard", 0, NULL);
    else if (strcmp (val, "GUARD_INTERVAL_1_16") == 0)
      g_object_set (dvbbasebin, "guard", 1, NULL);
    else if (strcmp (val, "GUARD_INTERVAL_1_8") == 0)
      g_object_set (dvbbasebin, "guard", 2, NULL);
    else if (strcmp (val, "GUARD_INTERVAL_1_4") == 0)
      g_object_set (dvbbasebin, "guard", 3, NULL);
    else
      g_object_set (dvbbasebin, "guard", 4, NULL);

    val = g_hash_table_lookup (params, "hierarchy");
    if (strcmp (val, "HIERARCHY_NONE") == 0)
      g_object_set (dvbbasebin, "hierarchy", 0, NULL);
    else if (strcmp (val, "HIERARCHY_1") == 0)
      g_object_set (dvbbasebin, "hierarchy", 1, NULL);
    else if (strcmp (val, "HIERARCHY_2") == 0)
      g_object_set (dvbbasebin, "hierarchy", 2, NULL);
    else if (strcmp (val, "HIERARCHY_4") == 0)
      g_object_set (dvbbasebin, "hierarchy", 3, NULL);
    else
      g_object_set (dvbbasebin, "hierarchy", 4, NULL);

    ret = TRUE;
  } else if (strcmp (type, "satellite") == 0) {
    gchar *val;

    ret = TRUE;

    g_object_set (dvbbasebin, "delsys", SYS_DVBS, NULL);

    val = g_hash_table_lookup (params, "polarity");
    if (val)
      g_object_set (dvbbasebin, "polarity", val, NULL);
    else
      ret = FALSE;

    val = g_hash_table_lookup (params, "diseqc-source");
    if (val)
      g_object_set (dvbbasebin, "diseqc-source", atoi (val), NULL);

    val = g_hash_table_lookup (params, "symbol-rate");
    if (val)
      g_object_set (dvbbasebin, "symbol-rate", atoi (val), NULL);
    else
      ret = FALSE;
  } else if (strcmp (type, "cable") == 0) {
    gchar *val;

    g_object_set (dvbbasebin, "delsys", SYS_DVBC_ANNEX_A, NULL);

    ret = TRUE;
    val = g_hash_table_lookup (params, "symbol-rate");
    if (val)
      g_object_set (dvbbasebin, "symbol-rate", atoi (val) / 1000, NULL);
    val = g_hash_table_lookup (params, "modulation");
    if (strcmp (val, "QPSK") == 0)
      g_object_set (dvbbasebin, "modulation", 0, NULL);
    else if (strcmp (val, "QAM_16") == 0)
      g_object_set (dvbbasebin, "modulation", 1, NULL);
    else if (strcmp (val, "QAM_32") == 0)
      g_object_set (dvbbasebin, "modulation", 2, NULL);
    else if (strcmp (val, "QAM_64") == 0)
      g_object_set (dvbbasebin, "modulation", 3, NULL);
    else if (strcmp (val, "QAM_128") == 0)
      g_object_set (dvbbasebin, "modulation", 4, NULL);
    else if (strcmp (val, "QAM_256") == 0)
      g_object_set (dvbbasebin, "modulation", 5, NULL);
    else
      g_object_set (dvbbasebin, "modulation", 6, NULL);
    val = g_hash_table_lookup (params, "code-rate-hp");
    if (strcmp (val, "FEC_NONE") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 0, NULL);
    else if (strcmp (val, "FEC_1_2") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 1, NULL);
    else if (strcmp (val, "FEC_2_3") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 2, NULL);
    else if (strcmp (val, "FEC_3_4") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 3, NULL);
    else if (strcmp (val, "FEC_4_5") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 4, NULL);
    else if (strcmp (val, "FEC_5_6") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 5, NULL);
    else if (strcmp (val, "FEC_6_7") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 6, NULL);
    else if (strcmp (val, "FEC_7_8") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 7, NULL);
    else if (strcmp (val, "FEC_8_9") == 0)
      g_object_set (dvbbasebin, "code-rate-hp", 8, NULL);
    else
      g_object_set (dvbbasebin, "code-rate-hp", 9, NULL);
    val = g_hash_table_lookup (params, "inversion");
    if (strcmp (val, "INVERSION_OFF") == 0)
      g_object_set (dvbbasebin, "inversion", 0, NULL);
    else if (strcmp (val, "INVERSION_ON") == 0)
      g_object_set (dvbbasebin, "inversion", 1, NULL);
    else
      g_object_set (dvbbasebin, "inversion", 2, NULL);
  } else if (strcmp (type, "atsc") == 0) {
    gchar *val;

    ret = TRUE;

    g_object_set (dvbbasebin, "delsys", SYS_ATSC, NULL);

    val = g_hash_table_lookup (params, "modulation");
    if (strcmp (val, "QAM_64") == 0)
      g_object_set (dvbbasebin, "modulation", 3, NULL);
    else if (strcmp (val, "QAM_256") == 0)
      g_object_set (dvbbasebin, "modulation", 5, NULL);
    else if (strcmp (val, "8VSB") == 0)
      g_object_set (dvbbasebin, "modulation", 7, NULL);
    else if (strcmp (val, "16VSB") == 0)
      g_object_set (dvbbasebin, "modulation", 8, NULL);
    else
      ret = FALSE;
  }

  destroy_channels_hash (channels);

beach:
  return ret;

unknown_channel:
  {
    /* FIXME: is channel name guaranteed to be ASCII or UTF-8? */
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND,
        _("Couldn't find details for DVB channel %s"), channel_name);
    destroy_channels_hash (channels);
    return FALSE;
  }
}
