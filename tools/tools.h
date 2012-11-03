/* GStreamer
 * Copyright (C) 2005 Benjamin Otte <otte@gnome.org>
 *
 * tools.h: header for common stuff of all GStreamer tools
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


#ifndef __GST_TOOLS_H__
#define __GST_TOOLS_H__

#include <stdlib.h>

#include <gst/gst.h>
#include "gst/gst-i18n-app.h"

/*
 * This is a kind of hacky way to make all the tools use the same version code.
 * If anyone knows a less hacky way to get this done, feel free to implement it.
 * 
 * It also includes all the files that all the tools require.
 */

static gboolean __gst_tools_version = FALSE;

#define GST_TOOLS_GOPTION_VERSION \
    { "version", 0, 0, G_OPTION_ARG_NONE, &__gst_tools_version, \
      N_("Print version information and exit"), NULL }

static void
gst_tools_print_version (void)
{
  if (__gst_tools_version) {
    gchar *version_str;

    version_str = gst_version_string ();
    g_print ("%s version %d.%d.%d\n", g_get_prgname (),
        GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);
    g_print ("%s\n", version_str);
    g_print ("%s\n", GST_PACKAGE_ORIGIN);
    g_free (version_str);
    exit (0);
  }
}

#endif /* __GST_TOOLS_H__ */
