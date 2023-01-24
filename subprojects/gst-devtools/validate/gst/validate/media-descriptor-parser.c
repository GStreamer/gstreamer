/* Gstreamer
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "media-descriptor-parser.h"
#include <string.h>

#include "gst-validate-internal.h"

struct _GstValidateMediaDescriptorParserPrivate
{
  gchar *xmlpath;

  gboolean in_stream;
  gchar *xmlcontent;
  GMarkupParseContext *parsecontext;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstValidateMediaDescriptorParser,
    gst_validate_media_descriptor_parser, GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR);

enum
{
  PROP_0,
  PROP_PATH,
  N_PROPERTIES
};

/* Private methods  and callbacks */
static gint
compare_frames (GstValidateMediaFrameNode * frm,
    GstValidateMediaFrameNode * frm1)
{
  if (frm->id < frm1->id)
    return -1;

  else if (frm->id == frm1->id)
    return 0;

  return 1;
}

static void
    deserialize_filenode
    (GstValidateMediaFileNode *
    filenode, const gchar ** names, const gchar ** values)
{
  gint i;
  for (i = 0; names[i] != NULL; i++) {
    if (g_strcmp0 (names[i], "uri") == 0)
      filenode->uri = g_strdup (values[i]);
    else if (g_strcmp0 (names[i], "id") == 0)
      filenode->id = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "frame-detection") == 0)
      filenode->frame_detection = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "duration") == 0)
      filenode->duration = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "seekable") == 0)
      filenode->seekable = (g_strcmp0 (values[i], "true") == 0);
  }
}

static GstValidateMediaStreamNode *
deserialize_streamnode (const gchar ** names, const gchar ** values)
{
  gint i;
  GstValidateMediaStreamNode
      * streamnode = g_new0 (GstValidateMediaStreamNode, 1);

  for (i = 0; names[i] != NULL; i++) {
    if (g_strcmp0 (names[i], "id") == 0)
      streamnode->id = g_strdup (values[i]);
    else if (g_strcmp0 (names[i], "caps") == 0)
      streamnode->caps = gst_caps_from_string (values[i]);
    else if (g_strcmp0 (names[i], "padname") == 0)
      streamnode->padname = g_strdup (values[i]);
  }


  return streamnode;
}

static GstValidateSegmentNode *
deserialize_segmentnode (const gchar ** names, const gchar ** values)
{
  gint i;
  GstValidateSegmentNode *node = g_new0 (GstValidateSegmentNode, 1);

  for (i = 0; names[i] != NULL; i++) {
    if (!g_strcmp0 (names[i], "next-frame-id"))
      node->next_frame_id = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "flags"))
      node->segment.flags = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "rate"))
      node->segment.rate = g_ascii_strtod (values[i], NULL);
    else if (!g_strcmp0 (names[i], "applied-rate"))
      node->segment.applied_rate = g_ascii_strtod (values[i], NULL);
    else if (!g_strcmp0 (names[i], "format"))
      node->segment.format = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "base"))
      node->segment.base = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "offset"))
      node->segment.offset = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "start"))
      node->segment.start = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "stop"))
      node->segment.stop = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "time"))
      node->segment.time = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "position"))
      node->segment.position = g_ascii_strtoull (values[i], NULL, 0);
    else if (!g_strcmp0 (names[i], "duration"))
      node->segment.duration = g_ascii_strtoull (values[i], NULL, 0);
  }

  return node;
}

static GstValidateMediaTagsNode *
deserialize_tagsnode (const gchar ** names, const gchar ** values)
{
  GstValidateMediaTagsNode *tagsnode = g_new0 (GstValidateMediaTagsNode, 1);

  return tagsnode;
}

static GstValidateMediaTagNode *
deserialize_tagnode (const gchar ** names, const gchar ** values)
{
  gint i;
  GstValidateMediaTagNode *tagnode = g_new0 (GstValidateMediaTagNode, 1);

  for (i = 0; names[i] != NULL; i++) {
    if (g_strcmp0 (names[i], "content") == 0)
      tagnode->taglist = gst_tag_list_new_from_string (values[i]);
  }

  return tagnode;
}

static GstValidateMediaFrameNode *
deserialize_framenode (const gchar ** names, const gchar ** values)
{
  gint i;

  GstValidateMediaFrameNode *framenode = g_new0 (GstValidateMediaFrameNode, 1);

/* *INDENT-OFF* */
#define IF_SET_UINT64_FIELD(name,fieldname) \
    if (g_strcmp0 (names[i], name) == 0) { \
      if (g_strcmp0 (values[i], "unknown") == 0)  \
        framenode->fieldname = GST_VALIDATE_UNKNOWN_UINT64; \
      else\
        framenode->fieldname = g_ascii_strtoull (values[i], NULL, 0); \
    }

  for (i = 0; names[i] != NULL; i++) {
    IF_SET_UINT64_FIELD ("id", id)
    else IF_SET_UINT64_FIELD ("offset", offset)
    else IF_SET_UINT64_FIELD ("offset-end", offset_end)
    else IF_SET_UINT64_FIELD ("duration", duration)
    else IF_SET_UINT64_FIELD ("pts", pts)
    else IF_SET_UINT64_FIELD ("dts", dts)
    else IF_SET_UINT64_FIELD ("running-time", running_time)
    else if (g_strcmp0 (names[i], "checksum") == 0)
      framenode->checksum = g_strdup (values[i]);
    else if (g_strcmp0 (names[i], "is-keyframe") == 0) {
      if (!g_ascii_strcasecmp (values[i], "true"))
        framenode->is_keyframe = TRUE;
      else if (!g_ascii_strcasecmp (values[i], "unknown"))
        framenode->is_keyframe = GST_VALIDATE_UNKNOWN_BOOL;
      else
        framenode->is_keyframe = FALSE;
    }
  }
/* *INDENT-ON* */

  framenode->buf = gst_buffer_new_wrapped (framenode->checksum,
      strlen (framenode->checksum) + 1);

  GST_BUFFER_OFFSET (framenode->buf) = framenode->offset;
  GST_BUFFER_OFFSET_END (framenode->buf) = framenode->offset_end;
  GST_BUFFER_DURATION (framenode->buf) = framenode->duration;
  GST_BUFFER_PTS (framenode->buf) = framenode->pts;
  GST_BUFFER_DTS (framenode->buf) = framenode->dts;

  if (framenode->is_keyframe) {
    GST_BUFFER_FLAG_UNSET (framenode->buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (framenode->buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  return framenode;
}


static void
on_end_element_cb (GMarkupParseContext * context,
    const gchar * element_name, gpointer user_data, GError ** error)
{
  GstValidateMediaDescriptorParserPrivate *priv =
      GST_VALIDATE_MEDIA_DESCRIPTOR_PARSER (user_data)->priv;

  if (g_strcmp0 (element_name, "stream") == 0) {
    priv->in_stream = FALSE;
  }
}

static void
on_start_element_cb (GMarkupParseContext * context,
    const gchar * element_name, const gchar ** attribute_names,
    const gchar ** attribute_values, gpointer user_data, GError ** error)
{
  GstValidateMediaFileNode
      * filenode =
      gst_validate_media_descriptor_get_file_node (GST_VALIDATE_MEDIA_DESCRIPTOR
      (user_data));

  GstValidateMediaDescriptorParserPrivate *priv =
      GST_VALIDATE_MEDIA_DESCRIPTOR_PARSER (user_data)->priv;

  if (g_strcmp0 (element_name, "file") == 0) {
    deserialize_filenode (filenode, attribute_names, attribute_values);
  } else if (g_strcmp0 (element_name, "stream") == 0) {
    GstValidateMediaStreamNode
        * node = deserialize_streamnode (attribute_names, attribute_values);
    priv->in_stream = TRUE;
    filenode->streams = g_list_prepend (filenode->streams, node);
  } else if (g_strcmp0 (element_name, "segment") == 0) {
    GstValidateMediaStreamNode *streamnode = filenode->streams->data;
    GstValidateSegmentNode *node =
        deserialize_segmentnode (attribute_names, attribute_values);

    streamnode->segments = g_list_append (streamnode->segments, node);

  } else if (g_strcmp0 (element_name, "frame") == 0) {
    GstValidateMediaStreamNode *streamnode = filenode->streams->data;

    streamnode->cframe = streamnode->frames =
        g_list_insert_sorted (streamnode->frames,
        deserialize_framenode (attribute_names, attribute_values),
        (GCompareFunc) compare_frames);
  } else if (g_strcmp0 (element_name, "tags") == 0) {
    if (priv->in_stream) {
      GstValidateMediaStreamNode *snode = (GstValidateMediaStreamNode *)
          filenode->streams->data;

      snode->tags = deserialize_tagsnode (attribute_names, attribute_values);
    } else {
      filenode->tags = deserialize_tagsnode (attribute_names, attribute_values);
    }
  } else if (g_strcmp0 (element_name, "tag") == 0) {
    GstValidateMediaTagsNode *tagsnode;

    if (priv->in_stream) {
      GstValidateMediaStreamNode *snode = (GstValidateMediaStreamNode *)
          filenode->streams->data;
      tagsnode = snode->tags;
    } else {
      tagsnode = filenode->tags;
    }

    tagsnode->tags = g_list_prepend (tagsnode->tags,
        deserialize_tagnode (attribute_names, attribute_values));
  }
}

static void
on_error_cb (GMarkupParseContext * context, GError * error, gpointer user_data)
{
  GST_ERROR ("Error parsing file: %s", error->message);
}

static const GMarkupParser content_parser = {
  on_start_element_cb,
  on_end_element_cb,
  NULL,
  NULL,
  &on_error_cb
};

static gboolean
_set_content (GstValidateMediaDescriptorParser * parser,
    const gchar * content, gsize size, GError ** error)
{
  GError *err = NULL;
  GstValidateMediaDescriptorParserPrivate *priv = parser->priv;

  priv->parsecontext = g_markup_parse_context_new (&content_parser,
      G_MARKUP_TREAT_CDATA_AS_TEXT, parser, NULL);

  if (g_markup_parse_context_parse (priv->parsecontext, content,
          size, &err) == FALSE)
    goto failed;

  return TRUE;

failed:
  g_propagate_error (error, err);
  return FALSE;
}

static gboolean
set_xml_path (GstValidateMediaDescriptorParser * parser, const gchar * path,
    GError ** error)
{
  gsize xmlsize;
  gchar *content;
  GError *err = NULL;
  GstValidateMediaDescriptorParserPrivate *priv = parser->priv;
  gboolean result;

  if (!g_file_get_contents (path, &content, &xmlsize, &err))
    goto failed;

  priv->xmlpath = g_strdup (path);

  result = _set_content (parser, content, xmlsize, error);
  g_free (content);
  return result;

failed:
  g_propagate_error (error, err);
  return FALSE;
}

/* GObject standard vmethods */
static void
dispose (GstValidateMediaDescriptorParser * parser)
{
  G_OBJECT_CLASS (gst_validate_media_descriptor_parser_parent_class)->dispose
      (G_OBJECT (parser));
}

static void
finalize (GstValidateMediaDescriptorParser * parser)
{
  GstValidateMediaDescriptorParserPrivate *priv;

  priv = parser->priv;

  g_free (priv->xmlpath);
  g_free (priv->xmlcontent);

  if (priv->parsecontext != NULL)
    g_markup_parse_context_free (priv->parsecontext);

  G_OBJECT_CLASS (gst_validate_media_descriptor_parser_parent_class)->finalize
      (G_OBJECT (parser));
}


static void
get_property (GObject * gobject, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      g_assert_not_reached ();
  }

}

static void
set_property (GObject * gobject, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      g_assert_not_reached ();
  }
}

static void
gst_validate_media_descriptor_parser_init (GstValidateMediaDescriptorParser *
    parser)
{
  GstValidateMediaDescriptorParserPrivate *priv;

  parser->priv = priv =
      gst_validate_media_descriptor_parser_get_instance_private (parser);

  priv->xmlpath = NULL;
}

static void
    gst_validate_media_descriptor_parser_class_init
    (GstValidateMediaDescriptorParserClass * self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = (void (*)(GObject * object)) dispose;
  object_class->finalize = (void (*)(GObject * object)) finalize;
  object_class->get_property = get_property;
  object_class->set_property = set_property;
}

/* Public methods */
GstValidateMediaDescriptorParser *
gst_validate_media_descriptor_parser_new (GstValidateRunner * runner,
    const gchar * xmlpath, GError ** error)
{
  GstValidateMediaDescriptorParser *parser;

  parser =
      g_object_new (GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_PARSER,
      "validate-runner", runner, NULL);

  if (set_xml_path (parser, xmlpath, error) == FALSE) {
    g_object_unref (parser);

    return NULL;
  }


  return parser;
}

GstValidateMediaDescriptorParser *
gst_validate_media_descriptor_parser_new_from_xml (GstValidateRunner * runner,
    const gchar * xml, GError ** error)
{
  GstValidateMediaDescriptorParser *parser;

  parser =
      g_object_new (GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_PARSER,
      "validate-runner", runner, NULL);
  if (_set_content (parser, xml, strlen (xml) * sizeof (gchar), error) == FALSE) {
    g_object_unref (parser);

    return NULL;
  }


  return parser;
}

gchar *gst_validate_media_descriptor_parser_get_xml_path
    (GstValidateMediaDescriptorParser * parser)
{
  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_PARSER (parser), NULL);

  return g_strdup (parser->priv->xmlpath);
}

gboolean
    gst_validate_media_descriptor_parser_add_stream
    (GstValidateMediaDescriptorParser * parser, GstPad * pad) {
  GList *tmp;
  gboolean ret = FALSE;
  GstCaps *caps;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_PARSER (parser),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) parser), FALSE);

  caps = gst_pad_query_caps (pad, NULL);
  for (tmp =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
              *) parser)->streams; tmp; tmp = tmp->next) {
    GstValidateMediaStreamNode *streamnode = (GstValidateMediaStreamNode *)
        tmp->data;

    if (streamnode->pad == NULL && gst_caps_is_equal (streamnode->caps, caps)) {
      ret = TRUE;
      streamnode->pad = gst_object_ref (pad);

      goto done;
    }
  }

done:
  if (caps != NULL)
    gst_caps_unref (caps);

  return ret;
}

gboolean
    gst_validate_media_descriptor_parser_all_stream_found
    (GstValidateMediaDescriptorParser * parser) {
  GList *tmp;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_PARSER (parser),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) parser), FALSE);

  for (tmp =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
              *) parser)->streams; tmp; tmp = tmp->next) {
    GstValidateMediaStreamNode *streamnode = (GstValidateMediaStreamNode *)
        tmp->data;

    if (streamnode->pad == NULL)
      return FALSE;

  }

  return TRUE;
}

gboolean
    gst_validate_media_descriptor_parser_add_taglist
    (GstValidateMediaDescriptorParser * parser, GstTagList * taglist) {
  GList *tmptag;
  GstValidateMediaTagsNode *tagsnode;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_PARSER (parser),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) parser), FALSE);
  g_return_val_if_fail (GST_IS_STRUCTURE (taglist), FALSE);

  tagsnode =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
          *) parser)->tags;

  for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
    if (gst_validate_tag_node_compare ((GstValidateMediaTagNode *)
            tmptag->data, taglist)) {
      GST_DEBUG ("Adding tag %" GST_PTR_FORMAT, taglist);
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
    gst_validate_media_descriptor_parser_all_tags_found
    (GstValidateMediaDescriptorParser * parser) {
  GList *tmptag;
  GstValidateMediaTagsNode *tagsnode;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_PARSER (parser),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) parser), FALSE);

  tagsnode =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
          *) parser)->tags;
  for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
    gchar *tag = NULL;

    tag = gst_tag_list_to_string (((GstValidateMediaTagNode *)
            tmptag->data)->taglist);
    if (((GstValidateMediaTagNode *)
            tmptag->data)->found == FALSE) {

      if (((GstValidateMediaTagNode *)
              tmptag->data)->taglist != NULL) {
        GST_DEBUG ("Tag not found %s", tag);
      } else {
        GST_DEBUG ("Tag not properly deserialized");
      }

      ret = FALSE;
    }

    GST_DEBUG ("Tag properly found %s", tag);
    g_free (tag);
  }

  return ret;
}
