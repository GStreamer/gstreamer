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

#include "media-descriptor-writer.h"
#include <string.h>

G_DEFINE_TYPE (GstMediaDescriptorWriter,
    gst_media_descriptor_writer, GST_TYPE_MEDIA_DESCRIPTOR);

#define STR_APPEND(arg, nb_white)  \
  tmpstr = res; \
  res = g_strdup_printf ("%s%*s%s%s", res, (nb_white), " ", (arg), "\n"); \
  g_free (tmpstr);

#define STR_APPEND0(arg) STR_APPEND((arg), 0)
#define STR_APPEND1(arg) STR_APPEND((arg), 2)
#define STR_APPEND2(arg) STR_APPEND((arg), 4)
#define STR_APPEND3(arg) STR_APPEND((arg), 6)
#define STR_APPEND4(arg) STR_APPEND((arg), 8)


enum
{
  PROP_0,
  PROP_PATH,
  N_PROPERTIES
};

struct _GstMediaDescriptorWriterPrivate
{
  GList *serialized_string;
  guint stream_id;
};

static void
finalize (GstMediaDescriptorWriter * writer)
{
  G_OBJECT_CLASS (gst_media_descriptor_writer_parent_class)->
      finalize (G_OBJECT (writer));
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
gst_media_descriptor_writer_init (GstMediaDescriptorWriter * writer)
{
  GstMediaDescriptorWriterPrivate *priv;

  writer->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (writer,
      GST_TYPE_MEDIA_DESCRIPTOR_WRITER, GstMediaDescriptorWriterPrivate);

  priv->serialized_string = NULL;
  priv->stream_id = 0;
}

static void
    gst_media_descriptor_writer_class_init
    (GstMediaDescriptorWriterClass * self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  g_type_class_add_private (self_class,
      sizeof (GstMediaDescriptorWriterPrivate));
  object_class->finalize = (void (*)(GObject * object)) finalize;
  object_class->get_property = get_property;
  object_class->set_property = set_property;
}

/* Private methods */
static gchar *
serialize_filenode (GstMediaDescriptorWriter * writer)
{
  gchar *res, *tmpstr, *caps_str, *tmpnode;
  GList *tmp, *tmp2;
  TagsNode *tagsnode;
  FileNode *filenode = ((GstMediaDescriptor *) writer)->filenode;

  res = g_markup_printf_escaped ("<file duration=\"%" G_GUINT64_FORMAT
      "\" frame-detection=\"%i\" uri=\"%s\" seekable=\"%s\">\n",
      filenode->duration, filenode->frame_detection, filenode->uri,
      filenode->seekable ? "true" : "false");

  if (filenode->caps)
    caps_str = gst_caps_to_string (filenode->caps);
  else
    caps_str = g_strdup ("");

  tmpnode = g_strdup_printf ("<streams caps=\"%s\">", caps_str);
  STR_APPEND1 (tmpnode);
  g_free (caps_str);
  g_free (tmpnode);

  for (tmp = filenode->streams; tmp; tmp = tmp->next) {
    GList *tmp3;
    StreamNode *snode = ((StreamNode *) tmp->data);

    STR_APPEND2 (snode->str_open);

    for (tmp2 = snode->frames; tmp2; tmp2 = tmp2->next) {
      STR_APPEND3 (((FrameNode *) tmp2->data)->str_open);
    }

    tagsnode = snode->tags;
    STR_APPEND3 (tagsnode->str_open);
    for (tmp3 = tagsnode->tags; tmp3; tmp3 = tmp3->next) {
      STR_APPEND4 (((TagNode *) tmp3->data)->str_open);
    }
    STR_APPEND3 (tagsnode->str_close);

    STR_APPEND2 (snode->str_close);
  }
  STR_APPEND1 ("</streams>");

  tagsnode = filenode->tags;
  STR_APPEND1 (tagsnode->str_open);
  for (tmp2 = tagsnode->tags; tmp2; tmp2 = tmp2->next) {
    STR_APPEND2 (((TagNode *) tmp2->data)->str_open);
  }
  STR_APPEND1 (tagsnode->str_close);

  tmpstr = res;
  res = g_strdup_printf ("%s%s", res, filenode->str_close);
  g_free (tmpstr);

  return res;
}

/* Public methods */
GstMediaDescriptorWriter *
gst_media_descriptor_writer_new (GstValidateRunner * runner,
    const gchar * uri, GstClockTime duration, gboolean seekable)
{
  GstMediaDescriptorWriter *writer;
  FileNode *fnode;

  writer =
      g_object_new (GST_TYPE_MEDIA_DESCRIPTOR_WRITER, "validate-runner", runner,
      NULL);

  fnode = ((GstMediaDescriptor *) writer)->filenode;
  fnode->uri = g_strdup (uri);
  fnode->duration = duration;
  fnode->seekable = seekable;
  fnode->str_open = NULL;

  fnode->str_close = g_markup_printf_escaped ("</file>");

  return writer;
}

static gboolean
gst_media_descriptor_writer_add_stream (GstMediaDescriptorWriter * writer,
    GstDiscovererStreamInfo * info)
{
  const gchar *stype;
  gboolean ret = FALSE;
  GstCaps *caps;
  gchar *capsstr = NULL;
  StreamNode *snode = NULL;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_WRITER (writer), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) writer)->filenode, FALSE);

  snode = g_slice_new0 (StreamNode);
  snode->frames = NULL;
  snode->cframe = NULL;

  snode->id = g_strdup (gst_discoverer_stream_info_get_stream_id (info));
  if (snode->id == NULL) {
    caps = gst_discoverer_stream_info_get_caps (info);
    capsstr = gst_caps_to_string (caps);

    g_slice_free (StreamNode, snode);
    GST_VALIDATE_REPORT (writer, FILE_NO_STREAM_ID,
        "Stream with caps: %s has no stream ID",
        capsstr);
    gst_caps_unref (caps);
    g_free (capsstr);

    return FALSE;
  }

  caps = gst_discoverer_stream_info_get_caps (info);
  snode->caps = caps;
  capsstr = gst_caps_to_string (caps);
  if (GST_IS_DISCOVERER_AUDIO_INFO (info)) {
    stype = "audio";
  } else if (GST_IS_DISCOVERER_VIDEO_INFO (info)) {
    if (gst_discoverer_video_info_is_image (GST_DISCOVERER_VIDEO_INFO (info)))
      stype = "image";
    else
      stype = "video";
  } else if (GST_IS_DISCOVERER_SUBTITLE_INFO (info)) {
    stype = "subtitle";
  } else {
    stype = "Unknown";
  }

  snode->str_open =
      g_markup_printf_escaped
      ("<stream type=\"%s\" caps=\"%s\" id=\"%s\">", stype, capsstr, snode->id);

  snode->str_close = g_markup_printf_escaped ("</stream>");

  ((GstMediaDescriptor *) writer)->filenode->streams =
      g_list_prepend (((GstMediaDescriptor *) writer)->filenode->streams,
      snode);

  if (gst_discoverer_stream_info_get_tags (info)) {
    gst_media_descriptor_writer_add_tags (writer, snode->id,
        gst_discoverer_stream_info_get_tags (info));
  }

  if (caps != NULL)
    gst_caps_unref (caps);
  g_free (capsstr);

  return ret;
}

GstMediaDescriptorWriter *
gst_media_descriptor_writer_new_discover (GstValidateRunner * runner,
    const gchar * uri, GError ** err)
{
  GList *tmp, *streams;
  GstDiscovererInfo *info;
  GstDiscoverer *discoverer;
  GstDiscovererStreamInfo *streaminfo;
  GstMediaDescriptorWriter *writer;

  discoverer = gst_discoverer_new (GST_SECOND * 60, err);

  if (discoverer == NULL) {
    GST_ERROR ("Could not create discoverer");

    return NULL;
  }

  info = gst_discoverer_discover_uri (discoverer, uri, err);
  if (info == NULL
      || gst_discoverer_info_get_result (info) != GST_DISCOVERER_OK) {

    GST_ERROR ("Could not discover URI: %s", uri);

    return NULL;
  }

  writer =
      gst_media_descriptor_writer_new (runner,
      gst_discoverer_info_get_uri (info),
      gst_discoverer_info_get_duration (info),
      gst_discoverer_info_get_seekable (info));

  if (gst_discoverer_info_get_tags (info))
    gst_media_descriptor_writer_add_taglist (writer,
        gst_discoverer_info_get_tags (info));

  streaminfo = gst_discoverer_info_get_stream_info (info);
  ((GstMediaDescriptor *) writer)->filenode->caps =
      gst_discoverer_stream_info_get_caps (GST_DISCOVERER_STREAM_INFO
      (streaminfo));

  streams = gst_discoverer_info_get_stream_list (info);
  for (tmp = streams; tmp; tmp = tmp->next)
    gst_media_descriptor_writer_add_stream (writer, tmp->data);
  gst_discoverer_stream_info_list_free(streams);


  return writer;
}

gboolean
gst_media_descriptor_writer_add_tags (GstMediaDescriptorWriter
    * writer, const gchar * stream_id, const GstTagList * taglist)
{
  TagsNode *tagsnode;
  TagNode *tagnode;
  GList *tmp, *tmptag;

  gchar *str_str = NULL;
  StreamNode *snode = NULL;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_WRITER (writer), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) writer)->filenode, FALSE);

  for (tmp = ((GstMediaDescriptor *) writer)->filenode->streams; tmp;
      tmp = tmp->next) {
    if (g_strcmp0 (((StreamNode *) tmp->data)->id, stream_id) == 0) {
      snode = tmp->data;

      break;
    }
  }

  if (snode == NULL) {
    GST_WARNING ("Could not find stream with id: %s", stream_id);

    return FALSE;
  }

  if (snode->tags == NULL) {
    tagsnode = g_slice_new0 (TagsNode);
    tagsnode->str_open = g_markup_printf_escaped ("<tags>");
    tagsnode->str_close = g_markup_printf_escaped ("</tags>");
    snode->tags = tagsnode;
  } else {
    tagsnode = snode->tags;

    for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
      if (tag_node_compare ((TagNode *) tmptag->data, taglist)) {
        GST_DEBUG ("Tag already in... not adding again %" GST_PTR_FORMAT,
            taglist);
        return TRUE;
      }
    }
  }

  tagnode = g_slice_new0 (TagNode);
  tagnode->taglist = gst_tag_list_copy (taglist);
  str_str = gst_tag_list_to_string (tagnode->taglist);
  tagnode->str_open =
      g_markup_printf_escaped ("<tag content=\"%s\"/>", str_str);
  tagsnode->tags = g_list_prepend (tagsnode->tags, tagnode);

  g_free (str_str);

  return FALSE;
}

gboolean
gst_media_descriptor_writer_add_pad (GstMediaDescriptorWriter *
    writer, GstPad * pad)
{
  GList *tmp;
  gboolean ret = FALSE;
  GstCaps *caps;
  gchar *capsstr = NULL, *padname = NULL;
  StreamNode *snode = NULL;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_WRITER (writer), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) writer)->filenode, FALSE);

  caps = gst_pad_get_current_caps (pad);
  for (tmp = ((GstMediaDescriptor *) writer)->filenode->streams; tmp;
      tmp = tmp->next) {
    StreamNode *streamnode = (StreamNode *) tmp->data;

    if (streamnode->pad == pad) {
      goto done;
    }
  }

  snode = g_slice_new0 (StreamNode);
  snode->frames = NULL;
  snode->cframe = NULL;

  snode->caps = gst_caps_ref (caps);
  snode->pad = gst_object_ref (pad);

  capsstr = gst_caps_to_string (caps);
  padname = gst_pad_get_name (pad);
  snode->str_open =
      g_markup_printf_escaped
      ("<stream padname=\"%s\" caps=\"%s\" id=\"%i\">", padname, capsstr, 0);

  snode->str_close = g_markup_printf_escaped ("</stream>");

  ((GstMediaDescriptor *) writer)->filenode->streams =
      g_list_prepend (((GstMediaDescriptor *) writer)->filenode->streams,
      snode);

done:
  if (caps != NULL)
    gst_caps_unref (caps);
  g_free (capsstr);
  g_free (padname);

  return ret;
}

gboolean
gst_media_descriptor_writer_add_taglist (GstMediaDescriptorWriter * writer,
    const GstTagList * taglist)
{
  gchar *str_str = NULL;
  TagsNode *tagsnode;
  TagNode *tagnode;
  GList *tmptag;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_WRITER (writer), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) writer)->filenode, FALSE);

  if (((GstMediaDescriptor *) writer)->filenode->tags == NULL) {
    tagsnode = g_slice_new0 (TagsNode);
    tagsnode->str_open = g_markup_printf_escaped ("<tags>");
    tagsnode->str_close = g_markup_printf_escaped ("</tags>");
    ((GstMediaDescriptor *) writer)->filenode->tags = tagsnode;
  } else {
    tagsnode = ((GstMediaDescriptor *) writer)->filenode->tags;
    for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
      if (tag_node_compare ((TagNode *) tmptag->data, taglist)) {
        GST_DEBUG ("Tag already in... not adding again %" GST_PTR_FORMAT,
            taglist);
        return TRUE;
      }
    }
  }

  tagnode = g_slice_new0 (TagNode);
  tagnode->taglist = gst_tag_list_copy (taglist);
  str_str = gst_tag_list_to_string (tagnode->taglist);
  tagnode->str_open =
      g_markup_printf_escaped ("<tag content=\"%s\"/>", str_str);
  tagsnode->tags = g_list_prepend (tagsnode->tags, tagnode);

  g_free (str_str);

  return FALSE;
}

gboolean
gst_media_descriptor_writer_add_frame (GstMediaDescriptorWriter
    * writer, GstPad * pad, GstBuffer * buf)
{
  GList *tmp;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_WRITER (writer), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) writer)->filenode, FALSE);

  ((GstMediaDescriptor *) writer)->filenode->frame_detection = TRUE;

  for (tmp = ((GstMediaDescriptor *) writer)->filenode->streams; tmp;
      tmp = tmp->next) {
    StreamNode *streamnode = (StreamNode *) tmp->data;

    if (streamnode->pad == pad) {
      guint id = g_list_length (streamnode->frames);
      FrameNode *fnode = g_slice_new0 (FrameNode);

      fnode->id = id;
      fnode->offset = GST_BUFFER_OFFSET (buf);
      fnode->offset_end = GST_BUFFER_OFFSET_END (buf);
      fnode->duration = GST_BUFFER_DURATION (buf);
      fnode->pts = GST_BUFFER_PTS (buf);
      fnode->dts = GST_BUFFER_DTS (buf);
      fnode->is_keyframe = (GST_BUFFER_FLAG_IS_SET (buf,
              GST_BUFFER_FLAG_DELTA_UNIT) == FALSE);

      fnode->str_open =
          g_markup_printf_escaped (" <frame duration=\"%" G_GUINT64_FORMAT
          "\" id=\"%i\" is-keyframe=\"%i\" offset=\"%" G_GUINT64_FORMAT
          "\" offset-end=\"%" G_GUINT64_FORMAT "\" pts=\"%"
          G_GUINT64_FORMAT "\"  dts=\"%" G_GUINT64_FORMAT "\" />",
          fnode->duration, id, fnode->is_keyframe,
          fnode->offset, fnode->offset_end, fnode->pts, fnode->dts);

      fnode->str_close = NULL;

      streamnode->frames = g_list_append (streamnode->frames, fnode);
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
gst_media_descriptor_writer_write (GstMediaDescriptorWriter *
    writer, const gchar * filename)
{
  gboolean ret = FALSE;
  gchar *serialized;

  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_WRITER (writer), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) writer)->filenode, FALSE);

  serialized = serialize_filenode (writer);


  if (g_file_set_contents (filename, serialized, -1, NULL) == TRUE)
    ret = TRUE;


  g_free (serialized);

  return ret;
}

gchar *
gst_media_descriptor_writer_serialize (GstMediaDescriptorWriter * writer)
{
  g_return_val_if_fail (GST_IS_MEDIA_DESCRIPTOR_WRITER (writer), FALSE);
  g_return_val_if_fail (((GstMediaDescriptor *) writer)->filenode, FALSE);

  return serialize_filenode (writer);
}
