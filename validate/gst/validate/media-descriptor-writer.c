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

#include <gst/validate/validate.h>
#include "media-descriptor-writer.h"
#include <string.h>

G_DEFINE_TYPE (GstMediaDescriptorWriter,
    gst_media_descriptor_writer, GST_TYPE_MEDIA_DESCRIPTOR);

#define STR_APPEND(arg, nb_white)  \
  g_string_append_printf (res, "%*s%s%s", (nb_white), " ", (arg), "\n"); \

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
  GstElement *pipeline;
  GstCaps *raw_caps;
  GMainLoop *loop;

  GList *parsers;
};

static void
finalize (GstMediaDescriptorWriter * writer)
{
  if (writer->priv->raw_caps)
    gst_caps_unref (writer->priv->raw_caps);

  if (writer->priv->parsers)
    gst_plugin_feature_list_free (writer->priv->parsers);

  G_OBJECT_CLASS (gst_media_descriptor_writer_parent_class)->finalize (G_OBJECT
      (writer));
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

  writer->priv->parsers =
      gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_PARSER,
      GST_RANK_MARGINAL);
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
  GString *res;
  gchar *tmpstr, *caps_str;
  GList *tmp, *tmp2;
  TagsNode *tagsnode;
  FileNode *filenode = ((GstMediaDescriptor *) writer)->filenode;

  tmpstr = g_markup_printf_escaped ("<file duration=\"%" G_GUINT64_FORMAT
      "\" frame-detection=\"%i\" uri=\"%s\" seekable=\"%s\">\n",
      filenode->duration, filenode->frame_detection, filenode->uri,
      filenode->seekable ? "true" : "false");

  if (filenode->caps)
    caps_str = gst_caps_to_string (filenode->caps);
  else
    caps_str = g_strdup ("");

  res = g_string_new (tmpstr);
  g_string_append_printf (res, "  <streams caps=\"%s\">", caps_str);
  for (tmp = filenode->streams; tmp; tmp = tmp->next) {
    GList *tmp3;
    StreamNode *snode = ((StreamNode *) tmp->data);

    STR_APPEND2 (snode->str_open);

    for (tmp2 = snode->frames; tmp2; tmp2 = tmp2->next) {
      STR_APPEND3 (((FrameNode *) tmp2->data)->str_open);
    }

    tagsnode = snode->tags;
    if (tagsnode) {
      STR_APPEND3 (tagsnode->str_open);
      for (tmp3 = tagsnode->tags; tmp3; tmp3 = tmp3->next) {
        STR_APPEND4 (((TagNode *) tmp3->data)->str_open);
      }
      STR_APPEND3 (tagsnode->str_close);
    }

    STR_APPEND2 (snode->str_close);
  }
  STR_APPEND1 ("</streams>");

  tagsnode = filenode->tags;
  STR_APPEND1 (tagsnode->str_open);
  for (tmp2 = tagsnode->tags; tmp2; tmp2 = tmp2->next) {
    STR_APPEND2 (((TagNode *) tmp2->data)->str_open);
  }
  STR_APPEND1 (tagsnode->str_close);

  g_string_append (res, filenode->str_close);

  return g_string_free (res, FALSE);
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
        "Stream with caps: %s has no stream ID", capsstr);
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

  if (writer->priv->raw_caps == NULL)
    writer->priv->raw_caps = gst_caps_copy (caps);
  else {
    writer->priv->raw_caps = gst_caps_merge (writer->priv->raw_caps,
        gst_caps_copy (caps));
  }
  gst_caps_unref (caps);
  g_free (capsstr);

  return ret;
}

static GstPadProbeReturn
_uridecodebin_probe (GstPad * pad, GstPadProbeInfo * info,
    GstMediaDescriptorWriter * writer)
{
  gst_media_descriptor_writer_add_frame (writer, pad, info->data);

  return GST_PAD_PROBE_OK;
}

static gboolean
_find_stream_id (GstPad * pad, GstEvent ** event,
    GstMediaDescriptorWriter * writer)
{
  if (GST_EVENT_TYPE (*event) == GST_EVENT_STREAM_START) {
    GList *tmp;
    StreamNode *snode = NULL;
    const gchar *stream_id;

    gst_event_parse_stream_start (*event, &stream_id);
    for (tmp = ((GstMediaDescriptor *) writer)->filenode->streams; tmp;
        tmp = tmp->next) {
      if (g_strcmp0 (((StreamNode *) tmp->data)->id, stream_id) == 0) {
        snode = tmp->data;

        break;
      }
    }

    if (!snode || snode->pad) {
      GST_VALIDATE_REPORT (writer, FILE_NO_STREAM_ID,
          "Got pad %s:%s where Discoverer found no stream ID",
          GST_DEBUG_PAD_NAME (pad));

      return TRUE;
    }

    snode->pad = gst_object_ref (pad);

    return FALSE;
  }

  return TRUE;
}

static inline GstElement *
_get_parser (GstMediaDescriptorWriter * writer, GstPad * pad)
{
  GList *parsers1, *parsers;
  GstElement *parser = NULL;
  GstElementFactory *parserfact = NULL;
  GstCaps *format;

  format = gst_pad_get_current_caps (pad);

  GST_DEBUG ("Getting list of parsers for format %" GST_PTR_FORMAT, format);
  parsers1 =
      gst_element_factory_list_filter (writer->priv->parsers, format,
      GST_PAD_SRC, FALSE);
  parsers =
      gst_element_factory_list_filter (parsers1, format, GST_PAD_SINK, FALSE);
  gst_plugin_feature_list_free (parsers1);

  if (G_UNLIKELY (parsers == NULL)) {
    GST_DEBUG ("Couldn't find any compatible parsers");
    goto beach;
  }

  /* Just pick the first one */
  parserfact = parsers->data;
  if (parserfact)
    parser = gst_element_factory_create (parserfact, NULL);

  gst_plugin_feature_list_free (parsers);

beach:
  if (format)
    gst_caps_unref (format);

  return parser;
}

static void
pad_added_cb (GstElement * decodebin, GstPad * pad,
    GstMediaDescriptorWriter * writer)
{
  GList *tmp;
  StreamNode *snode = NULL;
  GstPad *sinkpad, *srcpad;

  /*  Try to plug a parser so we have as much info as possible
   *  about the encoded stream. */
  GstElement *parser = _get_parser (writer, pad);
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);

  if (parser) {
    sinkpad = gst_element_get_static_pad (parser, "sink");
    gst_bin_add (GST_BIN (writer->priv->pipeline), parser);
    gst_element_sync_state_with_parent (parser);
    gst_pad_link (pad, sinkpad);

    srcpad = gst_element_get_static_pad (parser, "src");
  } else {
    srcpad = pad;
  }

  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  gst_bin_add (GST_BIN (writer->priv->pipeline), fakesink);
  gst_element_sync_state_with_parent (fakesink);
  gst_pad_link (srcpad, sinkpad);
  gst_pad_sticky_events_foreach (pad,
      (GstPadStickyEventsForeachFunction) _find_stream_id, writer);

  for (tmp = ((GstMediaDescriptor *) writer)->filenode->streams; tmp;
      tmp = tmp->next) {
    snode = tmp->data;
    if (snode->pad == pad && srcpad != pad) {
      gst_object_unref (pad);
      snode->pad = gst_object_ref (srcpad);
      break;
    }
  }

  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) _uridecodebin_probe, writer, NULL);
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message,
    GstMediaDescriptorWriter * writer)
{
  GMainLoop *loop = writer->priv->loop;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (writer->priv->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate-media-check.error");
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      GST_INFO ("Got EOS!");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (writer->priv->pipeline)) {
        GstState oldstate, newstate, pending;

        gst_message_parse_state_changed (message, &oldstate, &newstate,
            &pending);

        GST_DEBUG ("State changed (old: %s, new: %s, pending: %s)",
            gst_element_state_get_name (oldstate),
            gst_element_state_get_name (newstate),
            gst_element_state_get_name (pending));

        if (newstate == GST_STATE_PLAYING) {
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (writer->priv->pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL,
              "gst-validate-media-descriptor-writer.playing");
        }
      }

      break;
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      gst_message_parse_buffering (message, &percent);
      g_print ("%s %d%%  \r", "Buffering...", percent);

      /* no state management needed for live pipelines */
      if (percent == 100) {
        gst_element_set_state (writer->priv->pipeline, GST_STATE_PLAYING);
      } else {
        gst_element_set_state (writer->priv->pipeline, GST_STATE_PAUSED);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
_run_frame_analisis (GstMediaDescriptorWriter * writer,
    GstValidateRunner * runner, const gchar * uri)
{
  GstBus *bus;
  GstStateChangeReturn sret;
  GstValidateMonitor *monitor;

  GstElement *uridecodebin = gst_element_factory_make ("uridecodebin", NULL);

  writer->priv->pipeline = gst_pipeline_new ("frame-analisis");

  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (writer->priv->
          pipeline), runner, NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  g_object_set (uridecodebin, "uri", uri, "caps", writer->priv->raw_caps, NULL);
  g_signal_connect (uridecodebin, "pad-added", G_CALLBACK (pad_added_cb),
      writer);
  gst_bin_add (GST_BIN (writer->priv->pipeline), uridecodebin);

  writer->priv->loop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (writer->priv->pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) bus_callback, writer);
  sret = gst_element_set_state (writer->priv->pipeline, GST_STATE_PLAYING);
  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, we should get an error message posted on the bus */
      g_print ("Pipeline failed to go to PLAYING state\n");
      return FALSE;
    default:
      break;
  }

  g_main_loop_run (writer->priv->loop);
  sret = gst_element_set_state (writer->priv->pipeline, GST_STATE_NULL);
  gst_object_unref (writer->priv->pipeline);
  writer->priv->pipeline = NULL;
  g_main_loop_unref (writer->priv->loop);
  writer->priv->loop = NULL;

  return TRUE;
}

GstMediaDescriptorWriter *
gst_media_descriptor_writer_new_discover (GstValidateRunner * runner,
    const gchar * uri, gboolean full, GError ** err)
{
  GList *tmp, *streams = NULL;
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

    GST_ERROR ("Could not discover URI: %s (error: %s(", uri,
        err && *err ? (*err)->message : "Unkown");

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

  if (GST_IS_DISCOVERER_CONTAINER_INFO (streaminfo)) {
    ((GstMediaDescriptor *) writer)->filenode->caps =
        gst_discoverer_stream_info_get_caps (GST_DISCOVERER_STREAM_INFO
        (streaminfo));

    streams = gst_discoverer_info_get_stream_list (info);
    for (tmp = streams; tmp; tmp = tmp->next) {
      gst_media_descriptor_writer_add_stream (writer, tmp->data);
    }
  } else {
    gst_media_descriptor_writer_add_stream (writer, streaminfo);
  }

  if (streams == NULL)
    writer->priv->raw_caps =
        gst_caps_copy (((GstMediaDescriptor *) writer)->filenode->caps);
  gst_discoverer_stream_info_list_free (streams);


  if (full == TRUE)
    _run_frame_analisis (writer, runner, uri);

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
  GST_MEDIA_DESCRIPTOR_LOCK (writer);
  for (tmp = ((GstMediaDescriptor *) writer)->filenode->streams; tmp;
      tmp = tmp->next) {
    StreamNode *streamnode = (StreamNode *) tmp->data;

    if (streamnode->pad == pad) {
      GstMapInfo map;
      gchar *checksum;
      guint id = g_list_length (streamnode->frames);
      FrameNode *fnode = g_slice_new0 (FrameNode);

      g_assert (gst_buffer_map (buf, &map, GST_MAP_READ));
      checksum = g_compute_checksum_for_data (G_CHECKSUM_MD5,
          (const guchar *) map.data, map.size);
      gst_buffer_unmap (buf, &map);

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
          "\" id=\"%i\" is-keyframe=\"%s\" offset=\"%" G_GUINT64_FORMAT
          "\" offset-end=\"%" G_GUINT64_FORMAT "\" pts=\"%" G_GUINT64_FORMAT
          "\"  dts=\"%" G_GUINT64_FORMAT "\" checksum=\"%s\"/>",
          fnode->duration, id, fnode->is_keyframe ? "true" : "false",
          fnode->offset, fnode->offset_end, fnode->pts, fnode->dts, checksum);

      fnode->str_close = NULL;

      streamnode->frames = g_list_append (streamnode->frames, fnode);
      GST_MEDIA_DESCRIPTOR_UNLOCK (writer);

      g_free (checksum);
      return TRUE;
    }
  }
  GST_MEDIA_DESCRIPTOR_UNLOCK (writer);

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
