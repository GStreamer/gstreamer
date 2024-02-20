/* GStreamer
 * Copyright (C) 2007-2008 Wouter Cloetens <wouter@mind.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n-lib.h>

#include "gstsoupelements.h"
#include "gstsouphttpsrc.h"
#include "gstsouphttpclientsink.h"
#include "gstsouputils.h"

GST_DEBUG_CATEGORY (soup_utils_debug);
#define GST_CAT_DEFAULT soup_utils_debug

gboolean
soup_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;

  if (g_once_init_enter (&res)) {
    GST_DEBUG_CATEGORY_INIT (soup_utils_debug, "souputils", 0, "Soup utils");
#ifdef ENABLE_NLS
    GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
        LOCALEDIR);
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

    /* see https://bugzilla.gnome.org/show_bug.cgi?id=674885 */
    g_type_ensure (G_TYPE_SOCKET);
    g_type_ensure (G_TYPE_SOCKET_ADDRESS);
    g_type_ensure (G_TYPE_SOCKET_SERVICE);
    g_type_ensure (G_TYPE_SOCKET_FAMILY);
    g_type_ensure (G_TYPE_SOCKET_CLIENT);
    g_type_ensure (G_TYPE_RESOLVER);
    g_type_ensure (G_TYPE_PROXY_RESOLVER);
    g_type_ensure (G_TYPE_PROXY_ADDRESS);
    g_type_ensure (G_TYPE_TLS_CERTIFICATE);
    g_type_ensure (G_TYPE_TLS_CONNECTION);
    g_type_ensure (G_TYPE_TLS_DATABASE);
    g_type_ensure (G_TYPE_TLS_INTERACTION);

    g_once_init_leave (&res, TRUE);
  }
#ifndef LINK_SOUP
  if (!gst_soup_load_library ()) {
    GST_WARNING ("Failed to load libsoup library");
    return FALSE;
  }
#endif
  return TRUE;
}
