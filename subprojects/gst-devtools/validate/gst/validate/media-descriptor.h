/* GstValidate
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

#ifndef __GST_VALIDATE_MEDIA_DESCRIPTOR_H__
#define __GST_VALIDATE_MEDIA_DESCRIPTOR_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>
#include "gst-validate-report.h"

G_BEGIN_DECLS

#define GST_VALIDATE_UNKNOWN_UINT64 (G_MAXUINT64 - 2)
#define GST_VALIDATE_UNKNOWN_BOOL (G_MAXUINT32 - 2)
typedef struct
{
  /* Children */
  /* GstValidateMediaTagNode */
  GList *tags;

  gchar *str_open;
  gchar *str_close;
} GstValidateMediaTagsNode;

/* Parsing structures */
typedef struct
{
  /* Children */
  /* GstValidateMediaStreamNode */
  GList *streams;
  /* GstValidateMediaTagsNode */
  GstValidateMediaTagsNode *tags;

  /* attributes */
  guint64 id;
  gchar *uri;
  GstClockTime duration;
  gboolean frame_detection;
  gboolean skip_parsers;
  gboolean seekable;

  GstCaps *caps;

  gchar *str_open;
  gchar *str_close;
} GstValidateMediaFileNode;

typedef struct
{
  /* Children */
  GstTagList *taglist;

  /* Testing infos */
  gboolean found;

  gchar *str_open;
  gchar *str_close;
} GstValidateMediaTagNode;

typedef struct
{
  /* Children */
  /* GstValidateMediaFrameNode */
  GList *frames;

  /* GstValidateMediaTagsNode */
  GstValidateMediaTagsNode *tags;

  /* Attributes */
  GstCaps *caps;
  GList * segments;
  gchar *id;
  gchar *padname;

  /* Testing infos */
  GstPad *pad;
  GList *cframe;

  gchar *str_open;
  gchar *str_close;
} GstValidateMediaStreamNode;

typedef struct
{
  /* Attributes */
  guint64 id;
  guint64 offset;
  guint64 offset_end;
  GstClockTime duration;
  GstClockTime pts, dts;
  GstClockTime running_time;
  gboolean is_keyframe;

  GstBuffer *buf;

  gchar *checksum;
  gchar *str_open;
  gchar *str_close;
} GstValidateMediaFrameNode;

typedef struct
{
  gint next_frame_id;

  GstSegment segment;

  gchar *str_open;
  gchar *str_close;
} GstValidateSegmentNode;

GST_VALIDATE_API
void gst_validate_filenode_free (GstValidateMediaFileNode *
    filenode);
GST_VALIDATE_API
gboolean gst_validate_tag_node_compare (GstValidateMediaTagNode *
    tnode, const GstTagList * tlist);

GST_VALIDATE_API
GType gst_validate_media_descriptor_get_type (void);

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR (gst_validate_media_descriptor_get_type ())
#define GST_VALIDATE_MEDIA_DESCRIPTOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR, GstValidateMediaDescriptor))
#define GST_VALIDATE_MEDIA_DESCRIPTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR, GstValidateMediaDescriptorClass))
#define GST_IS_VALIDATE_MEDIA_DESCRIPTOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR))
#define GST_IS_VALIDATE_MEDIA_DESCRIPTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR))
#define GST_VALIDATE_MEDIA_DESCRIPTOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR, GstValidateMediaDescriptorClass))
#endif

#define GST_VALIDATE_MEDIA_DESCRIPTOR_GET_LOCK(obj)               (&GST_VALIDATE_MEDIA_DESCRIPTOR(obj)->lock)
#define GST_VALIDATE_MEDIA_DESCRIPTOR_LOCK(obj)                   g_mutex_lock(GST_VALIDATE_MEDIA_DESCRIPTOR_GET_LOCK(obj))
#define GST_VALIDATE_MEDIA_DESCRIPTOR_UNLOCK(obj)                 g_mutex_unlock(GST_VALIDATE_MEDIA_DESCRIPTOR_GET_LOCK(obj))

typedef struct _GstValidateMediaDescriptorPrivate
    GstValidateMediaDescriptorPrivate;

typedef struct
{
  GstObject parent;

  GstValidateMediaFileNode *filenode;

  GMutex lock;

  GstValidateMediaDescriptorPrivate *priv;
} GstValidateMediaDescriptor;

typedef struct
{
  GstObjectClass parent;

} GstValidateMediaDescriptorClass;

GST_VALIDATE_API
gboolean gst_validate_media_descriptors_compare (GstValidateMediaDescriptor *
    ref, GstValidateMediaDescriptor * compared);
GST_VALIDATE_API gboolean
gst_validate_media_descriptor_detects_frames (GstValidateMediaDescriptor *
    self);
GST_VALIDATE_API
gboolean gst_validate_media_descriptor_get_buffers (GstValidateMediaDescriptor *
    self, GstPad * pad, GCompareFunc compare_func, GList ** bufs);
GST_VALIDATE_API gboolean
gst_validate_media_descriptor_has_frame_info (GstValidateMediaDescriptor *
    self);
GST_VALIDATE_API GstClockTime
gst_validate_media_descriptor_get_duration (GstValidateMediaDescriptor * self);
GST_VALIDATE_API
gboolean gst_validate_media_descriptor_get_seekable (GstValidateMediaDescriptor
    * self);
GST_VALIDATE_API
GList *gst_validate_media_descriptor_get_pads (GstValidateMediaDescriptor *
    self);
G_END_DECLS
#endif
