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

gchar **
gst_gnomevfs_get_supported_uris (GnomeVFSOpenMode mode)
{
  GnomeVFSResult res;
  GnomeVFSHandle *handle;
  gchar *uris[] = {
    "http://localhost/bla",
    "file:///bla",
    "smb://localhost/bla",
    "ftp://localhost/bla",
    "sftp://localhost/bla",
    "nfs://localhost/bla",
    "ssh://localhost/bla",
    "bla://bla",
    NULL
  }
  , **result;
  gint n, r = 0;

  result = g_new (gchar *, 9);
  for (n = 0; uris[n] != NULL; n++) {
    res = gnome_vfs_open (&handle, uris[n], mode);
    if (res == GNOME_VFS_OK) {
      gnome_vfs_close (handle);
    }
    /* FIXME: do something with mode error */
    if (res != GNOME_VFS_ERROR_INVALID_URI) {
      gchar *protocol = g_strdup (uris[n]);
      gint n;

      for (n = 0; protocol[n] != '\0'; n++) {
        if (protocol[n] == ':') {
          protocol[n] = '\0';
          break;
        }
      }

      result[r++] = protocol;
    }
  }
  result[r] = NULL;

  return result;
}
