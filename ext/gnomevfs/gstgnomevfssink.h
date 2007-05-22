/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2003 Colin Walters <walters@verbum.org>
 *                    2005 Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_GNOME_VFS_SINK_H__
#define __GST_GNOME_VFS_SINK_H__

#include "gstgnomevfs.h"
#include "gstgnomevfsuri.h"
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_GNOME_VFS_SINK \
  (gst_gnome_vfs_sink_get_type())
#define GST_GNOME_VFS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GNOME_VFS_SINK,GstGnomeVFSSink))
#define GST_GNOME_VFS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GNOME_VFS_SINK,GstGnomeVFSSinkClass))
#define GST_IS_GNOME_VFS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GNOME_VFS_SINK))
#define GST_IS_GNOME_VFS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GNOME_VFS_SINK))

typedef struct _GstGnomeVFSSink GstGnomeVFSSink;
typedef struct _GstGnomeVFSSinkClass GstGnomeVFSSinkClass;

/**
 * GstGnomeVFSSink:
 *
 * Opaque data structure.
 */
struct _GstGnomeVFSSink
{
  GstBaseSink basesink;

  /*< private >*/

  /* uri */
  GnomeVFSURI *uri;
  gchar *uri_name;

  /* handle */
  GnomeVFSHandle *handle;

  /* whether we opened the handle ourselves */
  gboolean own_handle;

  guint64  current_pos;
};

struct _GstGnomeVFSSinkClass
{
  GstBaseSinkClass basesink_class;

  /* signals */
  gboolean (*erase_ask) (GstElement * element, GnomeVFSURI * uri);
};

GType gst_gnome_vfs_sink_get_type (void);

G_END_DECLS

#endif /* __GST_GNOME_VFS_SINK_H__ */

