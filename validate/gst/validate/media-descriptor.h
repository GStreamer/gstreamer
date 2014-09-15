/**
 * Gstreamer
 *
 * Copyright (c) 2012, Collabora Ltd.
 * Author: Thibault Saunier <thibault.saunier@collabora.com>
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

#ifndef __GST_MEDIA_DESCRIPTOR_H__
#define __GST_MEDIA_DESCRIPTOR_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>
#include "gst-validate-report.h"

G_BEGIN_DECLS

typedef struct
{
  /* Children */
  /* TagNode */
  GList *tags;

  gchar *str_open;
  gchar *str_close;
} TagsNode;

/* Parsing structures */
typedef struct
{
  /* Children */
  /* StreamNode */
  GList *streams;
  /* TagsNode */
  TagsNode *tags;

  /* attributes */
  guint64 id;
  gchar *uri;
  GstClockTime duration;
  gboolean frame_detection;
  gboolean seekable;

  GstCaps *caps;

  gchar *str_open;
  gchar *str_close;
} FileNode;

typedef struct
{
  /* Children */
  GstTagList *taglist;

  /* Testing infos */
  gboolean found;

  gchar *str_open;
  gchar *str_close;
} TagNode;

typedef struct
{
  /* Children */
  /* FrameNode */
  GList *frames;

  /* TagsNode */
  TagsNode *tags;

  /* Attributes */
  GstCaps *caps;
  gchar *id;
  gchar *padname;

  /* Testing infos */
  GstPad *pad;
  GList *cframe;

  gchar *str_open;
  gchar *str_close;
} StreamNode;

typedef struct
{
  /* Attributes */
  guint64 id;
  guint64 offset;
  guint64 offset_end;
  GstClockTime duration;
  GstClockTime pts, dts;
  gboolean is_keyframe;

  GstBuffer *buf;

  gchar *str_open;
  gchar *str_close;
} FrameNode;

void free_filenode (FileNode * filenode);
gboolean tag_node_compare (TagNode * tnode, const GstTagList * tlist);

GType gst_media_descriptor_get_type (void);

#define GST_TYPE_MEDIA_DESCRIPTOR (gst_media_descriptor_get_type ())
#define GST_MEDIA_DESCRIPTOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MEDIA_DESCRIPTOR, GstMediaDescriptor))
#define GST_MEDIA_DESCRIPTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MEDIA_DESCRIPTOR, GstMediaDescriptorClass))
#define GST_IS_MEDIA_DESCRIPTOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MEDIA_DESCRIPTOR))
#define GST_IS_MEDIA_DESCRIPTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MEDIA_DESCRIPTOR))
#define GST_MEDIA_DESCRIPTOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MEDIA_DESCRIPTOR, GstMediaDescriptorClass))

#define GST_MEDIA_DESCRIPTOR_GET_LOCK(obj)               (&GST_MEDIA_DESCRIPTOR(obj)->lock)
#define GST_MEDIA_DESCRIPTOR_LOCK(obj)                   g_mutex_lock(GST_MEDIA_DESCRIPTOR_GET_LOCK(obj))
#define GST_MEDIA_DESCRIPTOR_UNLOCK(obj)                 g_mutex_unlock(GST_MEDIA_DESCRIPTOR_GET_LOCK(obj))

typedef struct _GstMediaDescriptorPrivate GstMediaDescriptorPrivate;

typedef struct {
  GObject parent;

  FileNode *filenode;

  GMutex         lock;

  GstMediaDescriptorPrivate *priv;
} GstMediaDescriptor;

typedef struct {
  GObjectClass parent;

} GstMediaDescriptorClass;

gboolean gst_media_descriptors_compare (GstMediaDescriptor *ref,
                                        GstMediaDescriptor *compared);
gboolean gst_media_descriptor_detects_frames (GstMediaDescriptor * self);
gboolean gst_media_descriptor_get_buffers (GstMediaDescriptor * self,
    GstPad * pad, GCompareFunc compare_func, GList ** bufs);
GstClockTime gst_media_descriptor_get_duration (GstMediaDescriptor * self);
gboolean gst_media_descriptor_get_seekable (GstMediaDescriptor * self);
GList * gst_media_descriptor_get_pads (GstMediaDescriptor * self);
G_END_DECLS

#endif
