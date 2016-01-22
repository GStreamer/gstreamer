/* GstValidate
 *
 * Copyright (c) 2012, Collabora Ltd
 *    Author: Thibault Saunier <thibault.saunier@collabora.com>
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

#ifndef GST_MEDIA_DESCRIPTOR_WRITER_h
#define GST_MEDIA_DESCRIPTOR_WRITER_h

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "media-descriptor.h"

G_BEGIN_DECLS

GType gst_media_descriptor_writer_get_type (void);

#define GST_TYPE_MEDIA_DESCRIPTOR_WRITER            (gst_media_descriptor_writer_get_type ())
#define GST_MEDIA_DESCRIPTOR_WRITER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MEDIA_DESCRIPTOR_WRITER, GstMediaDescriptorWriter))
#define GST_MEDIA_DESCRIPTOR_WRITER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MEDIA_DESCRIPTOR_WRITER, GstMediaDescriptorWriterClass))
#define GST_IS_MEDIA_DESCRIPTOR_WRITER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MEDIA_DESCRIPTOR_WRITER))
#define GST_IS_MEDIA_DESCRIPTOR_WRITER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MEDIA_DESCRIPTOR_WRITER))
#define GST_MEDIA_DESCRIPTOR_WRITER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MEDIA_DESCRIPTOR_WRITER, GstMediaDescriptorWriterClass))

typedef struct _GstMediaDescriptorWriterPrivate GstMediaDescriptorWriterPrivate;


typedef struct {
  GstMediaDescriptor parent;

  GstMediaDescriptorWriterPrivate *priv;

} GstMediaDescriptorWriter;

typedef struct {

  GstMediaDescriptorClass parent;

} GstMediaDescriptorWriterClass;

GstMediaDescriptorWriter * gst_media_descriptor_writer_new_discover (GstValidateRunner *runner,
                                                                     const gchar *uri,
                                                                     gboolean full,
                                                                     gboolean handle_g_logs,
                                                                     GError **err);

GstMediaDescriptorWriter * gst_media_descriptor_writer_new          (GstValidateRunner *runner,
                                                                     const gchar *location,
                                                                     GstClockTime duration,
                                                                     gboolean seekable);

gchar * gst_media_descriptor_writer_get_xml_path        (GstMediaDescriptorWriter *writer);

gboolean gst_media_descriptor_writer_detects_frames     (GstMediaDescriptorWriter *writer);
GstClockTime gst_media_descriptor_writer_get_duration   (GstMediaDescriptorWriter *writer);
gboolean gst_media_descriptor_writer_get_seekable       (GstMediaDescriptorWriter * writer);

gboolean gst_media_descriptor_writer_add_pad            (GstMediaDescriptorWriter *writer,
                                                         GstPad *pad);
gboolean gst_media_descriptor_writer_add_taglist        (GstMediaDescriptorWriter *writer,
                                                         const GstTagList *taglist);
gboolean gst_media_descriptor_writer_add_frame          (GstMediaDescriptorWriter *writer,
                                                         GstPad *pad,
                                                         GstBuffer *buf);
gboolean gst_media_descriptor_writer_add_tags           (GstMediaDescriptorWriter *writer,
                                                         const gchar *stream_id,
                                                         const GstTagList *taglist);
gboolean gst_media_descriptor_writer_write              (GstMediaDescriptorWriter * writer,
                                                         const gchar * filename);
gchar * gst_media_descriptor_writer_serialize           (GstMediaDescriptorWriter *writer);


G_END_DECLS

#endif /* GST_MEDIA_DESCRIPTOR_WRITER_h */
