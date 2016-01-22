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

#ifndef GST_MEDIA_DESCRIPTOR_PARSER_h
#define GST_MEDIA_DESCRIPTOR_PARSER_h

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>
#include "media-descriptor.h"

G_BEGIN_DECLS

GType gst_media_descriptor_parser_get_type (void);

#define GST_TYPE_MEDIA_DESCRIPTOR_PARSER            (gst_media_descriptor_parser_get_type ())
#define GST_MEDIA_DESCRIPTOR_PARSER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MEDIA_DESCRIPTOR_PARSER, GstMediaDescriptorParser))
#define GST_MEDIA_DESCRIPTOR_PARSER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MEDIA_DESCRIPTOR_PARSER, GstMediaDescriptorParserClass))
#define GST_IS_MEDIA_DESCRIPTOR_PARSER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MEDIA_DESCRIPTOR_PARSER))
#define GST_IS_MEDIA_DESCRIPTOR_PARSER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MEDIA_DESCRIPTOR_PARSER))
#define GST_MEDIA_DESCRIPTOR_PARSER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MEDIA_DESCRIPTOR_PARSER, GstMediaDescriptorParserClass))

typedef struct _GstMediaDescriptorParserPrivate GstMediaDescriptorParserPrivate;


typedef struct {
  GstMediaDescriptor parent;

  GstMediaDescriptorParserPrivate *priv;

} GstMediaDescriptorParser;

typedef struct {

  GstMediaDescriptorClass parent;

} GstMediaDescriptorParserClass;

GstMediaDescriptorParser * gst_media_descriptor_parser_new (GstValidateRunner *runner,
                                                            const gchar * xmlpath,
                                                            GError **error);
GstMediaDescriptorParser *
gst_media_descriptor_parser_new_from_xml                   (GstValidateRunner * runner,
                                                            const gchar * xml,
                                                            GError ** error);
gchar * gst_media_descriptor_parser_get_xml_path        (GstMediaDescriptorParser *parser);
gboolean gst_media_descriptor_parser_add_stream         (GstMediaDescriptorParser *parser,
                                                                  GstPad *pad);
gboolean gst_media_descriptor_parser_add_taglist        (GstMediaDescriptorParser *parser,
                                                                  GstTagList *taglist);
gboolean gst_media_descriptor_parser_all_stream_found   (GstMediaDescriptorParser *parser);
gboolean gst_media_descriptor_parser_all_tags_found     (GstMediaDescriptorParser *parser);

G_END_DECLS

#endif /* GST_MEDIA_DESCRIPTOR_PARSER_h */
