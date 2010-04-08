/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gnomevfs.c: register gnomevfs elements
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include "gstgnomevfs.h"
#include "gstgnomevfssrc.h"
#include "gstgnomevfssink.h"

#include <libgnomevfs/gnome-vfs.h>
#include <gst/gst.h>

#include <string.h>

gchar *
gst_gnome_vfs_location_to_uri_string (const gchar * location)
{
  gchar *newloc, *ret;

  if (location == NULL)
    return NULL;

  /* already an URI string? */
  if (strstr (location, "://"))
    return g_strdup (location);

  newloc = gnome_vfs_escape_path_string (location);

  if (newloc && *newloc == '/') {
    ret = g_strdup_printf ("file://%s", newloc);
  } else {
    gchar *curdir;

    curdir = g_get_current_dir ();
    ret = g_strdup_printf ("file://%s/%s", curdir, newloc);
    g_free (curdir);
  }

  g_free (newloc);
  return ret;
}

GType
gst_gnome_vfs_uri_get_type (void)
{
  static GType type;            /* 0 */

  if (type == 0) {
    type = g_boxed_type_register_static ("GnomeVFSURI",
        (GBoxedCopyFunc) gnome_vfs_uri_ref,
        (GBoxedFreeFunc) gnome_vfs_uri_unref);
  }

  return type;
}

static gpointer
gst_gnome_vfs_handle_copy (gpointer handle)
{
  return handle;
}

static void
gst_gnome_vfs_handle_free (gpointer handle)
{
  return;
}

GType
gst_gnome_vfs_handle_get_type (void)
{
  static GType type;            /* 0 */

  if (type == 0) {
    /* hackish, but makes it show up nicely in gst-inspect */
    type = g_boxed_type_register_static ("GnomeVFSHandle",
        (GBoxedCopyFunc) gst_gnome_vfs_handle_copy,
        (GBoxedFreeFunc) gst_gnome_vfs_handle_free);
  }

  return type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* gnome vfs engine init */
  if (!gnome_vfs_initialized ()) {
    if (!gnome_vfs_init ()) {
      GST_WARNING ("Failed to initialize GnomeVFS - not registering plugin!");
      return FALSE;
    }
  }

  gst_plugin_add_dependency_simple (plugin, NULL, GNOME_VFS_MODULES_DIR, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  if (!gst_element_register (plugin, "gnomevfssrc", GST_RANK_MARGINAL,
          gst_gnome_vfs_src_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "gnomevfssink", GST_RANK_MARGINAL,
          gst_gnome_vfs_sink_get_type ()))
    return FALSE;

#ifdef ENABLE_NLS
/* FIXME: add category
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE, LOCALEDIR);
 */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gnomevfs",
    "elements to read from and write to  Gnome-VFS uri's",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
