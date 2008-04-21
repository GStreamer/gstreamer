/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2003 Colin Walters <walters@verbum.org>
 *
 * gstgnomevfssink.c: 
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
#include "config.h"
#endif

#include <libgnomevfs/gnome-vfs.h>
#include "gstgnomevfsuri.h"

#include <gst/gst.h>

/* FIXME: move this to source and sink and remove this file:
 * e.g. sinks cannot save to http:// and src cannot read from burn://
 */
static gpointer
_internal_get_supported_uris (gpointer data)
{
  /* no dav/davs in the list, because they don't appear to be reliable enough */
  const gchar *uris[] = {
    "http://localhost/bla",
    "https://localhost/bla",
    "file:///bla",
    "smb://localhost/bla",
    "ftp://localhost/bla",
    "sftp://localhost/bla",
    "nfs://localhost/bla",
    "ssh://localhost/bla",
    "burn://"
  };
  GnomeVFSURI *uri;
  gchar **result;
  gint n, r = 0;

  result = g_new0 (gchar *, G_N_ELEMENTS (uris) + 1);
  for (n = 0; n < G_N_ELEMENTS (uris); n++) {
    uri = gnome_vfs_uri_new (uris[n]);
    if (uri != NULL) {
      gchar *protocol = g_strdup (uris[n]);
      gint n;

      gnome_vfs_uri_unref (uri);
      for (n = 0; protocol[n] != '\0'; n++) {
        if (protocol[n] == ':') {
          protocol[n] = '\0';
          break;
        }
      }

      GST_DEBUG ("adding protocol '%s'", protocol);
      result[r++] = protocol;
    } else {
      GST_DEBUG ("could not create GnomeVfsUri from '%s'", uris[n]);
    }
  }
  result[r] = NULL;

  return result;
}

gchar **
gst_gnomevfs_get_supported_uris (void)
{
  static GOnce once = G_ONCE_INIT;

  g_once (&once, _internal_get_supported_uris, NULL);
  return (gchar **) once.retval;
}
