/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_SNAPSHOT_H__
#define __GST_SNAPSHOT_H__


#include <gst/gst.h>
#include <png.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_SNAPSHOT \
  (gst_snapshot_get_type())
#define GST_SNAPSHOT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SNAPSHOT,GstSnapshot))
#define GST_SNAPSHOT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SNAPSHOT,GstSnapshot))
#define GST_IS_SNAPSHOT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SNAPSHOT))
#define GST_IS_SNAPSHOT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SNAPSHOT))

typedef struct _GstSnapshot GstSnapshot;
typedef struct _GstSnapshotClass GstSnapshotClass;

struct _GstSnapshot {
  GstElement element;

  guint32 format;
  gint width;
  gint height;
  gint to_bpp;
  glong frame;
  glong cur_frame;
  const gchar *location;
  gboolean snapshot_asked;

  png_structp png_struct_ptr;
  png_infop png_info_ptr;

  GstPad *sinkpad,*srcpad;
};

struct _GstSnapshotClass {
  GstElementClass parent_class;

  void (*snapshot)  (GstElement *elem);
};

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_SNAPSHOT_H__ */
