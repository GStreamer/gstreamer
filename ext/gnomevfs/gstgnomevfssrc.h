/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2002 Kristian Rietveld <kris@gtk.org>
 *                    2002,2003 Colin Walters <walters@gnu.org>
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

#ifndef __GST_GNOME_VFS_SRC_H__
#define __GST_GNOME_VFS_SRC_H__

#include <gst/base/gstbasesrc.h>

#include "gstgnomevfs.h"
#include "gstgnomevfsuri.h"
#include <libgnomevfs/gnome-vfs.h>

G_BEGIN_DECLS

#define GST_TYPE_GNOME_VFS_SRC \
  (gst_gnome_vfs_src_get_type())
#define GST_GNOME_VFS_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GNOME_VFS_SRC,GstGnomeVFSSrc))
#define GST_GNOME_VFS_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GNOME_VFS_SRC,GstGnomeVFSSrcClass))
#define GST_IS_GNOME_VFS_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GNOME_VFS_SRC))
#define GST_IS_GNOME_VFS_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GNOME_VFS_SRC))

typedef struct _GstGnomeVFSSrc      GstGnomeVFSSrc;
typedef struct _GstGnomeVFSSrcClass GstGnomeVFSSrcClass;

/**
 * GstGnomeVFSSrc:
 *
 * Opaque data structure.
 */
struct _GstGnomeVFSSrc
{
  GstBaseSrc basesrc;

  /* uri, file, ... */
  GnomeVFSURI *uri;
  gchar *uri_name;
  GnomeVFSContext *context;
  GnomeVFSHandle *handle;
  gboolean own_handle;
  gboolean interrupted;
  GnomeVFSFileOffset curoffset; /* current offset in file */
  gboolean seekable;

  /* shoutcast/icecast metadata extraction handling */
  gboolean iradio_mode;
  gboolean http_callbacks_pushed;

  gchar *iradio_name;
  gchar *iradio_genre;
  gchar *iradio_url;
  gchar *iradio_title;
};

struct _GstGnomeVFSSrcClass
{
  GstBaseSrcClass  basesrc_class;
};

GType gst_gnome_vfs_src_get_type (void);

G_END_DECLS

#endif /* __GST_GNOME_VFS_SRC_H__ */


