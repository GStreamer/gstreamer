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

#ifndef GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_h
#define GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_h

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "media-descriptor.h"

G_BEGIN_DECLS

GST_VALIDATE_API
GType gst_validate_media_descriptor_writer_get_type (void);

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER            (gst_validate_media_descriptor_writer_get_type ())
#define GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER, GstValidateMediaDescriptorWriter))
#define GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER, GstValidateMediaDescriptorWriterClass))
#define GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER))
#define GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER))
#define GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER, GstValidateMediaDescriptorWriterClass))
#endif

typedef struct _GstValidateMediaDescriptorWriterPrivate GstValidateMediaDescriptorWriterPrivate;


typedef struct {
  GstValidateMediaDescriptor parent;

  GstValidateMediaDescriptorWriterPrivate *priv;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];

} GstValidateMediaDescriptorWriter;

typedef struct {

  GstValidateMediaDescriptorClass parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];

} GstValidateMediaDescriptorWriterClass;

/**
 * GstValidateMediaDescriptorWriterFlags
 */
typedef enum
{
    GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_NONE         = 1 << 0,
    GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_NO_PARSER    = 1 << 1,
    GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_FULL         = 1 << 2,
    GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_HANDLE_GLOGS = 1 << 3,
} GstValidateMediaDescriptorWriterFlags;

GST_VALIDATE_API
GstValidateMediaDescriptorWriter * gst_validate_media_descriptor_writer_new_discover (GstValidateRunner *runner,
                                                                     const gchar *uri,
                                                                     GstValidateMediaDescriptorWriterFlags flags,
                                                                     GError **err);

GST_VALIDATE_API
GstValidateMediaDescriptorWriter * gst_validate_media_descriptor_writer_new          (GstValidateRunner *runner,
                                                                     const gchar *location,
                                                                     GstClockTime duration,
                                                                     gboolean seekable);

gchar * gst_validate_media_descriptor_writer_get_xml_path        (GstValidateMediaDescriptorWriter *writer);

gboolean gst_validate_media_descriptor_writer_detects_frames     (GstValidateMediaDescriptorWriter *writer);
GstClockTime gst_validate_media_descriptor_writer_get_duration   (GstValidateMediaDescriptorWriter *writer);
gboolean gst_validate_media_descriptor_writer_get_seekable       (GstValidateMediaDescriptorWriter * writer);

GST_VALIDATE_API
gboolean gst_validate_media_descriptor_writer_add_pad            (GstValidateMediaDescriptorWriter *writer,
                                                         GstPad *pad);
GST_VALIDATE_API
gboolean gst_validate_media_descriptor_writer_add_taglist        (GstValidateMediaDescriptorWriter *writer,
                                                         const GstTagList *taglist);
GST_VALIDATE_API
gboolean gst_validate_media_descriptor_writer_add_frame          (GstValidateMediaDescriptorWriter *writer,
                                                         GstPad *pad,
                                                         GstBuffer *buf);
GST_VALIDATE_API
gboolean gst_validate_media_descriptor_writer_add_tags           (GstValidateMediaDescriptorWriter *writer,
                                                         const gchar *stream_id,
                                                         const GstTagList *taglist);
GST_VALIDATE_API
gboolean gst_validate_media_descriptor_writer_write              (GstValidateMediaDescriptorWriter * writer,
                                                         const gchar * filename);
GST_VALIDATE_API
gchar * gst_validate_media_descriptor_writer_serialize           (GstValidateMediaDescriptorWriter *writer);


G_END_DECLS

#endif /* GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_h */
