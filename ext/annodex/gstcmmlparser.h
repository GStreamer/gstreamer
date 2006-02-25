/*
 * gstcmmlparser.h - GStreamer CMML document parser
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#ifndef __GST_CMML_PARSER_H__
#define __GST_CMML_PARSER_H__

#include <libxml/parser.h>
#include <glib.h>

#include "gstcmmltag.h"

typedef struct _GstCmmlParser GstCmmlParser;
typedef enum _GstCmmlParserMode GstCmmlParserMode;

typedef void (*GstCmmlParserPreambleCallback) (void *user_data,
    const guchar * xml_preamble, const guchar * cmml_attrs);

typedef void (*GstCmmlParserCmmlEndCallback) (void *user_data);

typedef void (*GstCmmlParserStreamCallback) (void *user_data,
    GstCmmlTagStream * stream);

typedef void (*GstCmmlParserHeadCallback) (void *user_data,
    GstCmmlTagHead * head);

typedef void (*GstCmmlParserClipCallback) (void *user_data,
    GstCmmlTagClip * clip);

enum _GstCmmlParserMode
{
  GST_CMML_PARSER_ENCODE,
  GST_CMML_PARSER_DECODE
};

struct _GstCmmlParser
{
  GstCmmlParserMode mode;

  xmlParserCtxtPtr context;

  const gchar *preamble;
  guint preamble_size;

  void *user_data;
  GstCmmlParserPreambleCallback preamble_callback;
  GstCmmlParserStreamCallback stream_callback;
  GstCmmlParserCmmlEndCallback cmml_end_callback;
  GstCmmlParserHeadCallback head_callback;
  GstCmmlParserClipCallback clip_callback;
};

void gst_cmml_parser_init (void);

GstCmmlParser *gst_cmml_parser_new (GstCmmlParserMode mode);
void gst_cmml_parser_free (GstCmmlParser * parser);

gboolean gst_cmml_parser_parse_chunk (GstCmmlParser * parser,
    const gchar * data, guint size, GError ** error);

guchar *gst_cmml_parser_tag_stream_to_string (GstCmmlParser * parser,
    GstCmmlTagStream * stream);

guchar *gst_cmml_parser_tag_head_to_string (GstCmmlParser * parser,
    GstCmmlTagHead * head);

guchar *gst_cmml_parser_tag_clip_to_string (GstCmmlParser * parser,
    GstCmmlTagClip * clip);

guchar *gst_cmml_parser_tag_object_to_string (GstCmmlParser * parser,
    GObject * tag);

#endif /* __GST_CMML_PARSER_H__ */
