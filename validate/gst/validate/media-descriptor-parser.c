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

#include "media-descriptor-parser.h"
#include <string.h>

G_DEFINE_TYPE (GstMediaDescriptorParser, gst_media_descriptor_parser,
    GST_TYPE_MEDIA_DESCRIPTOR);

enum
{
  PROP_0,
  PROP_PATH,
  N_PROPERTIES
};

struct _GstMediaDescriptorParserPrivate
{
  gchar *xmlpath;

  gboolean in_stream;
  gchar *xmlcontent;
  GMarkupParseContext *parsecontext;
};

/* Private methods  and callbacks */
static gint
compare_frames (FrameNode * frm, FrameNode * frm1)
{
  if (frm->id < frm1->id)
    return -1;

  else if (frm->id == frm1->id)
    return 0;

  return 1;
}

static void
deserialize_filenode (FileNode * filenode,
    const gchar ** names, const gchar ** values)
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
      filenode->seekable = (gboolean) g_strcmp0 (values[i], "false");
  }
}

static StreamNode *
deserialize_streamnode (const gchar ** names, const gchar ** values)
{
  gint i;
  StreamNode *streamnode = g_slice_new0 (StreamNode);

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

static TagsNode *
deserialize_tagsnode (const gchar ** names, const gchar ** values)
{
  TagsNode *tagsnode = g_slice_new0 (TagsNode);

  return tagsnode;
}

static TagNode *
deserialize_tagnode (const gchar ** names, const gchar ** values)
{
  gint i;
  TagNode *tagnode = g_slice_new0 (TagNode);

  for (i = 0; names[i] != NULL; i++) {
    if (g_strcmp0 (names[i], "content") == 0)
      tagnode->taglist = gst_tag_list_new_from_string (values[i]);
  }

  return tagnode;
}

static FrameNode *
deserialize_framenode (const gchar ** names, const gchar ** values)
{
  gint i;

  FrameNode *framenode = g_slice_new0 (FrameNode);

  for (i = 0; names[i] != NULL; i++) {
    if (g_strcmp0 (names[i], "id") == 0)
      framenode->id = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "offset") == 0)
      framenode->offset = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "offset-end") == 0)
      framenode->offset_end = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "duration") == 0)
      framenode->duration = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "pts") == 0)
      framenode->pts = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "dts") == 0)
      framenode->dts = g_ascii_strtoull (values[i], NULL, 0);
    else if (g_strcmp0 (names[i], "checksum") == 0)
      framenode->checksum = g_strdup (values[i]);
    else if (g_strcmp0 (names[i], "is-keyframe") == 0) {
      if (!g_ascii_strcasecmp (values[i], "true"))
        framenode->is_keyframe = TRUE;
      else
        framenode->is_keyframe = FALSE;
    }
  }

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


static gboolean
frame_node_compare (FrameNode * fnode, GstBuffer * buf, GstBuffer * expected)
{
  if (expected != NULL) {
    GST_BUFFER_OFFSET (expected) = fnode->offset;
    GST_BUFFER_OFFSET_END (expected) = fnode->offset_end;
    GST_BUFFER_DURATION (expected) = fnode->duration;
    GST_BUFFER_PTS (expected) = fnode->pts;
    GST_BUFFER_DTS (expected) = fnode->dts;
    if (fnode->is_keyframe)
      GST_BUFFER_FLAG_UNSET (expected, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  if ((fnode->offset == GST_BUFFER_OFFSET (buf) &&
          fnode->offset_end == GST_BUFFER_OFFSET_END (buf) &&
          fnode->duration == GST_BUFFER_DURATION (buf) &&
          fnode->pts == GST_BUFFER_PTS (buf) &&
          fnode->dts == GST_BUFFER_DTS (buf) &&
          fnode->is_keyframe == GST_BUFFER_FLAG_IS_SET (buf,
              GST_BUFFER_FLAG_DELTA_UNIT)) == FALSE) {
    return TRUE;
  }

  return FALSE;
}

static void
on_end_element_cb (GMarkupParseContext * context,
    const gchar * element_name, gpointer user_data, GError ** error)
{
  GstMediaDescriptorParserPrivate *priv =
      GST_MEDIA_DESCRIPTOR_PARSER (user_data)->priv;

  if (g_strcmp0 (element_name, "stream") == 0) {
    priv->in_stream = FALSE;
  }
}

static void
on_start_element_cb (GMarkupParseContext * context,
    const gchar * element_name, const gchar ** attribute_names,
    const gchar ** attribute_values, gpointer user_data, GError ** error)
{
  FileNode *filenode = GST_MEDIA_DESCRIPTOR (user_data)->filenode;

  GstMediaDescriptorParserPrivate *priv =
      GST_MEDIA_DESCRIPTOR_PARSER (user_data)->priv;

  if (g_strcmp0 (element_name, "file") == 0) {
    deserialize_filenode (filenode, attribute_names, attribute_values);
  } else if (g_strcmp0 (element_name, "stream") == 0) {
    StreamNode *node =
        deserialize_streamnode (attribute_names, attribute_values);
    priv->in_stream = TRUE;
    filenode->streams = g_list_prepend (filenode->streams, node);
  } else if (g_strcmp0 (element_name, "frame") == 0) {
    StreamNode *streamnode = filenode->streams->data;

    streamnode->cframe = streamnode->frames =
        g_list_insert_sorted (streamnode->frames,
        deserialize_framenode (attribute_names, attribute_values),
        (GCompareFunc) compare_frames);
  } else if (g_strcmp0 (element_name, "tags") == 0) {
    if (priv->in_stream) {
      StreamNode *snode = (StreamNode *) filenode->streams->data;

      snode->tags = deserialize_tagsnode (attribute_names, attribute_values);
    } else {
      filenode->tags = deserialize_tagsnode (attribute_names, attribute_values);
    }
  } else if (g_strcmp0 (element_name, "tag") == 0) {
    TagsNode *tagsnode;

    if (priv->in_stream) {
      StreamNode *snode = (StreamNode *) filenode->streams->data;
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
_set_content (GstMediaDescriptorParser * parser,
    const gchar * content, gsize size, GError ** error)
{
  GError *err = NULL;
  GstMediaDescriptorParserPrivate *priv = parser->priv;

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
set_xml_path (GstMediaDescriptorParser * parser, const gchar * path,
    GError ** error)
{
  gsize xmlsize;
  gchar *content;
  GError *err = NULL;
  GstMediaDescriptorParserPrivate *priv = parser->priv;

  if (!g_file_get_contents (path, &content, &xmlsize, &err))
    goto failed;

  priv->xmlpath = g_strdup (path);

  return _set_content (parser, content, xmlsize, error);

failed:
  g_propagate_error (error, err);
  return FALSE;
}

/* GObject standard vmethods */
static void
dispose (GstMediaDescriptorParser * parser)
{
  G_OBJECT_CLASS (gst_media_descriptor_parser_parent_class)->dispose (G_OBJECT
      (parser));
}

static void
finalize (GstMediaDescriptorParser * parser)
{
  GstMediaDescriptorParserPrivate *priv;

  priv = parser->priv;

  g_free (priv->xmlpath);
  g_free (priv->xmlcontent);

  if (priv->parsecontext != NULL)
    g_markup_parse_context_free (priv->parsecontext);

  G_OBJECT_CLASS (gst_media_descriptor_parser_parent_class)->finalize (G_OBJECT
      (parser));
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
gst_media_descriptor_parser_init (GstMediaDescriptorParser * parser)
{
  GstMediaDescriptorParserPrivate *priv;

  parser->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (parser,
      GST_TYPE_MEDIA_DESCRIPTOR_PARSER, GstMediaDescriptorParserPrivate);

  priv->xmlpath = NULL;
}

static void
gst_media_descriptor_parser_class_init (GstMediaDescriptorParserClass *
    self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  g_type_class_add_private (self_class,
      sizeof (GstMediaDescriptorParserPrivate));
  object_class->dispose = (void (*)(GObject * object)) dispose;
  object_class->finalize = (void (*)(GObject * object)) finalize;
  object_class->get_property = get_property;
  object_class->set_property = set_property;
}

/* Public methods */
GstMediaDescriptorParser *
gst_media_descriptor_parser_new (GstValidateRunner * runner,
    const gchar * xmlpath, GError ** error)
{
  GstMediaDescriptorParser *parser;

  parser = g_object_new (GST_TYPE_MEDIA_DESCRIPTOR_PARSER, "validate-runner",
      runner, NULL);

  if (set_xml_path (parser, xmlpath, error) == FALSE) {
    g_object_unref (parser);

    return NULL;
  }


  return parser;
}

GstMediaDescriptorParser *
gst_media_descriptor_parser_new_from_xml (GstValidateRunner * runner,
    const gchar * xml, GError ** error)
{
  GstMediaDescriptorParser *parser;

  parser = g_object_new (GST_TYPE_MEDIA_DESCRIPTOR_PARSER, "validate-runner",
      runner, NULL);
  if (_set_content (parser, g_strdup (xml), strlen (xml) * sizeof (gchar),
          error) == FALSE) {
    g_object_unref (parser);

    return NULL;
  }


  return parser;
}

gchar *
gst_media_descriptor_parser_get_xml_path (GstMediaDescriptorParser * parser)
{
  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_PARSER (parser), NULL);

  return g_strdup (parser->priv->xmlpath);
}

gboolean
gst_media_descriptor_parser_add_stream (GstMediaDescriptorParser * parser,
    GstPad * pad)
{
  GList *tmp;
  gboolean ret = FALSE;
  GstCaps *caps;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_PARSER (parser), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) parser)->filenode, FALSE);

  caps = gst_pad_query_caps (pad, NULL);
  for (tmp = ((GstMediaDescriptor *) parser)->filenode->streams; tmp;
      tmp = tmp->next) {
    StreamNode *streamnode = (StreamNode *) tmp->data;

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
gst_media_descriptor_parser_all_stream_found (GstMediaDescriptorParser * parser)
{
  GList *tmp;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_PARSER (parser), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) parser)->filenode, FALSE);

  for (tmp = ((GstMediaDescriptor *) parser)->filenode->streams; tmp;
      tmp = tmp->next) {
    StreamNode *streamnode = (StreamNode *) tmp->data;

    if (streamnode->pad == NULL)
      return FALSE;

  }

  return TRUE;
}

gboolean
gst_media_descriptor_parser_add_frame (GstMediaDescriptorParser * parser,
    GstPad * pad, GstBuffer * buf, GstBuffer * expected)
{
  GList *tmp;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_PARSER (parser), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) parser)->filenode, FALSE);

  for (tmp = ((GstMediaDescriptor *) parser)->filenode->streams; tmp;
      tmp = tmp->next) {
    StreamNode *streamnode = (StreamNode *) tmp->data;

    if (streamnode->pad == pad && streamnode->cframe) {
      FrameNode *fnode = streamnode->cframe->data;

      streamnode->cframe = streamnode->cframe->next;
      return frame_node_compare (fnode, buf, expected);
    }
  }

  return FALSE;
}

gboolean
gst_media_descriptor_parser_add_taglist (GstMediaDescriptorParser * parser,
    GstTagList * taglist)
{
  GList *tmptag;
  TagsNode *tagsnode;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_PARSER (parser), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) parser)->filenode, FALSE);
  g_return_val_if_fail (GST_IS_STRUCTURE (taglist), FALSE);

  tagsnode = ((GstMediaDescriptor *) parser)->filenode->tags;

  for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
    if (tag_node_compare ((TagNode *) tmptag->data, taglist)) {
      GST_DEBUG ("Adding tag %" GST_PTR_FORMAT, taglist);
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
gst_media_descriptor_parser_all_tags_found (GstMediaDescriptorParser * parser)
{
  GList *tmptag;
  TagsNode *tagsnode;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_PARSER (parser), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) parser)->filenode, FALSE);

  tagsnode = ((GstMediaDescriptor *) parser)->filenode->tags;
  for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
    gchar *tag = NULL;

    tag = gst_tag_list_to_string (((TagNode *) tmptag->data)->taglist);
    if (((TagNode *) tmptag->data)->found == FALSE) {

      if (((TagNode *) tmptag->data)->taglist != NULL) {
        GST_DEBUG ("Tag not found %s", tag);
      } else {
        GST_DEBUG ("Tag not not properly deserialized");
      }

      ret = FALSE;
    }

    GST_DEBUG ("Tag properly found found %s", tag);
    g_free (tag);
  }

  return ret;
}
