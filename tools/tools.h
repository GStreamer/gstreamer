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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_TOOLS_H__
#define __GST_TOOLS_H__

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

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

#define GST_TOOLS_POPT_VERSION {"version", '\0', POPT_ARG_NONE, \
    &__gst_tools_version, 0, N_("print version information and exit"), NULL}

void
gst_tools_print_version (const char *program)
{
  if (__gst_tools_version) {
    guint major, minor, micro;
    
    gst_version (&major, &minor, &micro);
    g_print ("GStreamer (%s) %s %s\n\n", program, GST_PACKAGE, GST_VERSION);
    g_print ("provided by %s\n", GST_ORIGIN);
    g_print ("release %s\n", GST_VERSION_RELEASE);
    g_print ("using GStreamer Core Library version %u.%u.%u\n", major, minor, micro);
    exit (0);
  }
  g_set_prgname (program);
}

#endif /* __GST_TOOLS_H__ */
