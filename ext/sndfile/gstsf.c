/* GStreamer libsndfile plugin
 * Copyright (C) 2003 Andy Wingo <wingo at pobox dot com>
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

#include <gst/gst-i18n-plugin.h>
#include <string.h>

#include "gstsf.h"


GType
gst_sf_major_types_get_type (void)
{
  static GType sf_major_types_type = 0;
  static GEnumValue *sf_major_types = NULL;

  if (!sf_major_types_type) {
    SF_FORMAT_INFO format_info;
    int k, count;

    sf_command (NULL, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof (int));

    sf_major_types = g_new0 (GEnumValue, count + 1);

    for (k = 0; k < count; k++) {
      format_info.format = k;
      sf_command (NULL, SFC_GET_FORMAT_MAJOR, &format_info,
          sizeof (format_info));
      sf_major_types[k].value = format_info.format;
      sf_major_types[k].value_name = g_strdup (format_info.name);
      sf_major_types[k].value_nick = g_strdup (format_info.extension);

      /* Irritatingly enough, there exist major_types with the same extension. Let's
         just hope that sndfile gives us the list in alphabetical order, as it
         currently does. */
      if (k > 0
          && strcmp (sf_major_types[k].value_nick,
              sf_major_types[k - 1].value_nick) == 0) {
        g_free ((gchar *) sf_major_types[k].value_nick);
        sf_major_types[k].value_nick =
            g_strconcat (sf_major_types[k - 1].value_nick, "-",
            sf_major_types[k].value_name, NULL);
        g_strcanon ((gchar *) sf_major_types[k].value_nick,
            G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-", '-');
      }
    }

    sf_major_types_type =
        g_enum_register_static ("GstSndfileMajorTypes", sf_major_types);
  }
  return sf_major_types_type;
}

GType
gst_sf_minor_types_get_type (void)
{
  static GType sf_minor_types_type = 0;
  static GEnumValue *sf_minor_types = NULL;

  if (!sf_minor_types_type) {
    SF_FORMAT_INFO format_info;
    int k, count;

    sf_command (NULL, SFC_GET_FORMAT_SUBTYPE_COUNT, &count, sizeof (int));

    sf_minor_types = g_new0 (GEnumValue, count + 1);

    for (k = 0; k < count; k++) {
      format_info.format = k;
      sf_command (NULL, SFC_GET_FORMAT_SUBTYPE, &format_info,
          sizeof (format_info));
      sf_minor_types[k].value = format_info.format;
      sf_minor_types[k].value_name = g_strdup (format_info.name);
      sf_minor_types[k].value_nick = g_ascii_strdown (format_info.name, -1);
      g_strcanon ((gchar *) sf_minor_types[k].value_nick,
          G_CSET_a_2_z G_CSET_DIGITS "-", '-');
    }

    sf_minor_types_type =
        g_enum_register_static ("GstSndfileMinorTypes", sf_minor_types);
  }
  return sf_minor_types_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  if (!gst_element_register (plugin, "sfsink", GST_RANK_NONE,
          gst_sf_sink_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "sfsrc", GST_RANK_NONE,
          gst_sf_src_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    sndfile,
    "use libsndfile to read and write audio from and to files",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
