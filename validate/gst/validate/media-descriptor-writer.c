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

#include <gst/validate/validate.h>
#include "media-descriptor-writer.h"
#include <string.h>

G_DEFINE_TYPE (GstValidateMediaDescriptorWriter,
    gst_validate_media_descriptor_writer, GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR);

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

struct _GstValidateMediaDescriptorWriterPrivate
{
  GstElement *pipeline;
  GstCaps *raw_caps;
  GMainLoop *loop;

  GList *parsers;
};

static void
finalize (GstValidateMediaDescriptorWriter * writer)
{
  if (writer->priv->raw_caps)
    gst_caps_unref (writer->priv->raw_caps);

  if (writer->priv->parsers)
    gst_plugin_feature_list_free (writer->priv->parsers);

  G_OBJECT_CLASS (gst_validate_media_descriptor_writer_parent_class)->finalize
      (G_OBJECT (writer));
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
gst_validate_media_descriptor_writer_init (GstValidateMediaDescriptorWriter *
    writer)
{
  GstValidateMediaDescriptorWriterPrivate *priv;


  writer->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (writer,
      GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER,
      GstValidateMediaDescriptorWriterPrivate);

  writer->priv->parsers =
      gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_PARSER,
      GST_RANK_MARGINAL);
}

static void
    gst_validate_media_descriptor_writer_class_init
    (GstValidateMediaDescriptorWriterClass * self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  g_type_class_add_private (self_class,
      sizeof (GstValidateMediaDescriptorWriterPrivate));
  object_class->finalize = (void (*)(GObject * object)) finalize;
  object_class->get_property = get_property;
  object_class->set_property = set_property;
}

/* Private methods */
static gchar *
serialize_filenode (GstValidateMediaDescriptorWriter * writer)
{
  GString *res;
  gchar *tmpstr, *caps_str;
  GList *tmp, *tmp2;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode
      * tagsnode;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaFileNode
      * filenode = ((GstValidateMediaDescriptor *) writer)->filenode;

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
  g_free (caps_str);
  g_free (tmpstr);
  for (tmp = filenode->streams; tmp; tmp = tmp->next) {
    GList *tmp3;
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        * snode =
        (
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
            *) tmp->data);

    STR_APPEND2 (snode->str_open);

    for (tmp2 = snode->frames; tmp2; tmp2 = tmp2->next) {
      STR_APPEND3 (((GstValidateMediaGstValidateMediaGstValidateMediaFrameNode
                  *) tmp2->data)->str_open);
    }

    tagsnode = snode->tags;
    if (tagsnode) {
      STR_APPEND3 (tagsnode->str_open);
      for (tmp3 = tagsnode->tags; tmp3; tmp3 = tmp3->next) {
        STR_APPEND4 (((GstValidateMediaGstValidateMediaGstValidateMediaTagNode
                    *) tmp3->data)->str_open);
      }
      STR_APPEND3 (tagsnode->str_close);
    }

    STR_APPEND2 (snode->str_close);
  }
  STR_APPEND1 ("</streams>");

  tagsnode = filenode->tags;
  if (tagsnode) {
    STR_APPEND1 (tagsnode->str_open);
    for (tmp2 = tagsnode->tags; tmp2; tmp2 = tmp2->next) {
      STR_APPEND2 (((GstValidateMediaGstValidateMediaGstValidateMediaTagNode *)
              tmp2->data)->str_open);
    }
    STR_APPEND1 (tagsnode->str_close);
  }

  g_string_append (res, filenode->str_close);

  return g_string_free (res, FALSE);
}

/* Should be called with GST_VALIDATE_MEDIA_DESCRIPTOR_LOCK */
static
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
    * gst_validate_media_descriptor_find_stream_node_by_pad
    (GstValidateMediaDescriptor * md, GstPad * pad)
{
  GList *tmp;

  for (tmp = md->filenode->streams; tmp; tmp = tmp->next) {
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        * streamnode =
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        *) tmp->data;

    if (streamnode->pad == pad) {
      return streamnode;
    }
  }

  return NULL;
}

/* Public methods */
GstValidateMediaDescriptorWriter *
gst_validate_media_descriptor_writer_new (GstValidateRunner * runner,
    const gchar * uri, GstClockTime duration, gboolean seekable)
{
  GstValidateMediaDescriptorWriter *writer;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaFileNode
      * fnode;

  writer =
      g_object_new (GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER,
      "validate-runner", runner, NULL);

  fnode = ((GstValidateMediaDescriptor *) writer)->filenode;
  fnode->uri = g_strdup (uri);
  fnode->duration = duration;
  fnode->seekable = seekable;
  fnode->str_open = NULL;

  fnode->str_close = g_markup_printf_escaped ("</file>");

  return writer;
}

static gboolean
    gst_validate_media_descriptor_writer_add_stream
    (GstValidateMediaDescriptorWriter * writer, GstDiscovererStreamInfo * info)
{
  const gchar *stype;
  gboolean ret = FALSE;
  GstCaps *caps;
  gchar *capsstr = NULL;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
      * snode = NULL;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (((GstValidateMediaDescriptor *) writer)->filenode,
      FALSE);

  snode =
      g_slice_new0
      (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode);
  snode->frames = NULL;
  snode->cframe = NULL;

  snode->id = g_strdup (gst_discoverer_stream_info_get_stream_id (info));
  if (snode->id == NULL) {
    caps = gst_discoverer_stream_info_get_caps (info);
    capsstr = gst_caps_to_string (caps);

    g_slice_free
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode,
        snode);
    GST_VALIDATE_REPORT (writer, FILE_NO_STREAM_ID,
        "Stream with caps: %s has no stream ID", capsstr);
    gst_caps_unref (caps);
    g_free (capsstr);

    return FALSE;
  }

  caps = gst_discoverer_stream_info_get_caps (info);
  snode->caps = caps;           /* Pass ownership */
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

  ((GstValidateMediaDescriptor *) writer)->filenode->streams =
      g_list_prepend (((GstValidateMediaDescriptor *) writer)->
      filenode->streams, snode);

  if (gst_discoverer_stream_info_get_tags (info)) {
    gst_validate_media_descriptor_writer_add_tags (writer, snode->id,
        gst_discoverer_stream_info_get_tags (info));
  }

  if (writer->priv->raw_caps == NULL)
    writer->priv->raw_caps = gst_caps_copy (caps);
  else {
    writer->priv->raw_caps = gst_caps_merge (writer->priv->raw_caps,
        gst_caps_copy (caps));
  }
  g_free (capsstr);

  return ret;
}

static GstPadProbeReturn
_uridecodebin_probe (GstPad * pad, GstPadProbeInfo * info,
    GstValidateMediaDescriptorWriter * writer)
{
  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER) {
    gst_validate_media_descriptor_writer_add_frame (writer, pad, info->data);
  } else if (GST_PAD_PROBE_INFO_TYPE (info) &
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_SEGMENT:{
        const GstSegment *segment;
        GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
            * streamnode;

        streamnode =
            gst_validate_media_descriptor_find_stream_node_by_pad (
            (GstValidateMediaDescriptor *)
            writer, pad);
        if (streamnode) {
          gst_event_parse_segment (event, &segment);
          gst_segment_copy_into (segment, &streamnode->segment);
        }
        break;
      }
      default:
        break;
    }
  } else {
    g_assert_not_reached ();
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
_find_stream_id (GstPad * pad, GstEvent ** event,
    GstValidateMediaDescriptorWriter * writer)
{
  if (GST_EVENT_TYPE (*event) == GST_EVENT_STREAM_START) {
    GList *tmp;
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        * snode = NULL;
    const gchar *stream_id;

    gst_event_parse_stream_start (*event, &stream_id);
    for (tmp = ((GstValidateMediaDescriptor *) writer)->filenode->streams; tmp;
        tmp = tmp->next) {
      if (g_strcmp0 ((
                  (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
                      *)
                  tmp->data)->id, stream_id) == 0) {
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
_get_parser (GstValidateMediaDescriptorWriter * writer, GstPad * pad)
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
    GstValidateMediaDescriptorWriter * writer)
{
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
      * snode = NULL;
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

  if (srcpad != pad) {
    snode =
        gst_validate_media_descriptor_find_stream_node_by_pad (
        (GstValidateMediaDescriptor *)
        writer, pad);
    if (snode) {
      gst_object_unref (pad);
      snode->pad = gst_object_ref (srcpad);
    }
  }

  gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) _uridecodebin_probe, writer, NULL);
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message,
    GstValidateMediaDescriptorWriter * writer)
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
_run_frame_analysis (GstValidateMediaDescriptorWriter * writer,
    GstValidateRunner * runner, const gchar * uri)
{
  GstBus *bus;
  GstStateChangeReturn sret;
  GstValidateMonitor *monitor;

  GstElement *uridecodebin = gst_element_factory_make ("uridecodebin", NULL);

  writer->priv->pipeline = gst_pipeline_new ("frame-analysis");

  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (writer->
          priv->pipeline), runner, NULL);
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
  gst_element_set_state (writer->priv->pipeline, GST_STATE_NULL);
  gst_object_unref (writer->priv->pipeline);
  writer->priv->pipeline = NULL;
  g_main_loop_unref (writer->priv->loop);
  writer->priv->loop = NULL;
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);

  return TRUE;
}

GstValidateMediaDescriptorWriter *
gst_validate_media_descriptor_writer_new_discover (GstValidateRunner * runner,
    const gchar * uri, gboolean full, gboolean handle_g_logs, GError ** err)
{
  GList *tmp, *streams = NULL;
  GstDiscovererInfo *info = NULL;
  GstDiscoverer *discoverer;
  GstDiscovererStreamInfo *streaminfo = NULL;
  GstValidateMediaDescriptorWriter *writer = NULL;
  GstValidateMediaDescriptor *media_descriptor;
  const GstTagList *tags;
  GError *error = NULL;

  discoverer = gst_discoverer_new (GST_SECOND * 60, &error);

  if (discoverer == NULL) {
    GST_ERROR ("Could not create discoverer");
    g_propagate_error (err, error);
    return NULL;
  }

  info = gst_discoverer_discover_uri (discoverer, uri, &error);

  if (error) {
    GST_ERROR ("Could not discover URI: %s (error: %s)", uri, error->message);
    g_propagate_error (err, error);
    goto out;
  } else {
    GstDiscovererResult result = gst_discoverer_info_get_result (info);
    switch (result) {
      case GST_DISCOVERER_OK:
        break;
      case GST_DISCOVERER_URI_INVALID:
        GST_ERROR ("URI is not valid");
        goto out;
      case GST_DISCOVERER_TIMEOUT:
        GST_ERROR ("Analyzing URI timed out\n");
        goto out;
      case GST_DISCOVERER_BUSY:
        GST_ERROR ("Discoverer was busy\n");
        goto out;
      case GST_DISCOVERER_MISSING_PLUGINS:
      {
        gint i = 0;
        const gchar **installer_details =
            gst_discoverer_info_get_missing_elements_installer_details (info);
        GST_ERROR ("Missing plugins");
        while (installer_details[i]) {
          GST_ERROR ("(%s)", installer_details[i]);
          i++;
        }

        goto out;
      }
      default:
        break;
    }
  }

  streaminfo = gst_discoverer_info_get_stream_info (info);

  if (streaminfo) {
    writer =
        gst_validate_media_descriptor_writer_new (runner,
        gst_discoverer_info_get_uri (info),
        gst_discoverer_info_get_duration (info),
        gst_discoverer_info_get_seekable (info));

    if (handle_g_logs)
      gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (writer));

    tags = gst_discoverer_info_get_tags (info);
    if (tags)
      gst_validate_media_descriptor_writer_add_taglist (writer, tags);

    if (GST_IS_DISCOVERER_CONTAINER_INFO (streaminfo)) {
      ((GstValidateMediaDescriptor *) writer)->filenode->caps =
          gst_discoverer_stream_info_get_caps (GST_DISCOVERER_STREAM_INFO
          (streaminfo));

      streams = gst_discoverer_info_get_stream_list (info);
      for (tmp = streams; tmp; tmp = tmp->next) {
        gst_validate_media_descriptor_writer_add_stream (writer, tmp->data);
      }
    } else {
      gst_validate_media_descriptor_writer_add_stream (writer, streaminfo);
    }
  } else {
    GST_VALIDATE_REPORT (writer, FILE_NO_STREAM_INFO,
        "Discoverer info, does not contain the stream info");
    goto out;
  }

  media_descriptor = (GstValidateMediaDescriptor *) writer;
  if (streams == NULL && media_descriptor->filenode->caps)
    writer->priv->raw_caps = gst_caps_copy (media_descriptor->filenode->caps);
  gst_discoverer_stream_info_list_free (streams);


  if (full == TRUE)
    _run_frame_analysis (writer, runner, uri);

out:
  if (info)
    gst_discoverer_info_unref (info);
  if (streaminfo)
    gst_discoverer_stream_info_unref (streaminfo);
  g_object_unref (discoverer);
  return writer;
}

gboolean
gst_validate_media_descriptor_writer_add_tags (GstValidateMediaDescriptorWriter
    * writer, const gchar * stream_id, const GstTagList * taglist)
{
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode
      * tagsnode;
  GstValidateMediaGstValidateMediaGstValidateMediaTagNode *tagnode;
  GList *tmp, *tmptag;

  gchar *str_str = NULL;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
      * snode = NULL;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (((GstValidateMediaDescriptor *) writer)->filenode,
      FALSE);

  for (tmp = ((GstValidateMediaDescriptor *) writer)->filenode->streams; tmp;
      tmp = tmp->next) {
    if (g_strcmp0 ((
                (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
                    *) tmp->data)->id, stream_id) == 0) {
      snode = tmp->data;

      break;
    }
  }

  if (snode == NULL) {
    GST_WARNING ("Could not find stream with id: %s", stream_id);

    return FALSE;
  }

  if (snode->tags == NULL) {
    tagsnode =
        g_slice_new0
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode);
    tagsnode->str_open = g_markup_printf_escaped ("<tags>");
    tagsnode->str_close = g_markup_printf_escaped ("</tags>");
    snode->tags = tagsnode;
  } else {
    tagsnode = snode->tags;

    for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
      if (gst_validate_gst_validate_gst_validate_gst_validate_tag_node_compare (
              (GstValidateMediaGstValidateMediaGstValidateMediaTagNode *)
              tmptag->data, taglist)) {
        GST_DEBUG ("Tag already in... not adding again %" GST_PTR_FORMAT,
            taglist);
        return TRUE;
      }
    }
  }

  tagnode =
      g_slice_new0 (GstValidateMediaGstValidateMediaGstValidateMediaTagNode);
  tagnode->taglist = gst_tag_list_copy (taglist);
  str_str = gst_tag_list_to_string (tagnode->taglist);
  tagnode->str_open =
      g_markup_printf_escaped ("<tag content=\"%s\"/>", str_str);
  tagsnode->tags = g_list_prepend (tagsnode->tags, tagnode);

  g_free (str_str);

  return FALSE;
}

gboolean
gst_validate_media_descriptor_writer_add_pad (GstValidateMediaDescriptorWriter *
    writer, GstPad * pad)
{
  GList *tmp;
  gboolean ret = FALSE;
  GstCaps *caps;
  gchar *capsstr = NULL, *padname = NULL;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
      * snode = NULL;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (((GstValidateMediaDescriptor *) writer)->filenode,
      FALSE);

  caps = gst_pad_get_current_caps (pad);
  for (tmp = ((GstValidateMediaDescriptor *) writer)->filenode->streams; tmp;
      tmp = tmp->next) {
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        * streamnode =
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        *) tmp->data;

    if (streamnode->pad == pad) {
      goto done;
    }
  }

  snode =
      g_slice_new0
      (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode);
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

  ((GstValidateMediaDescriptor *) writer)->filenode->streams =
      g_list_prepend (((GstValidateMediaDescriptor *) writer)->
      filenode->streams, snode);

done:
  if (caps != NULL)
    gst_caps_unref (caps);
  g_free (capsstr);
  g_free (padname);

  return ret;
}

gboolean
    gst_validate_media_descriptor_writer_add_taglist
    (GstValidateMediaDescriptorWriter * writer, const GstTagList * taglist) {
  gchar *str_str = NULL;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode
      * tagsnode;
  GstValidateMediaGstValidateMediaGstValidateMediaTagNode *tagnode;
  GList *tmptag;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (((GstValidateMediaDescriptor *) writer)->filenode,
      FALSE);

  if (((GstValidateMediaDescriptor *) writer)->filenode->tags == NULL) {
    tagsnode =
        g_slice_new0
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode);
    tagsnode->str_open = g_markup_printf_escaped ("<tags>");
    tagsnode->str_close = g_markup_printf_escaped ("</tags>");
    ((GstValidateMediaDescriptor *) writer)->filenode->tags = tagsnode;
  } else {
    tagsnode = ((GstValidateMediaDescriptor *) writer)->filenode->tags;
    for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
      if (gst_validate_gst_validate_gst_validate_gst_validate_tag_node_compare (
              (GstValidateMediaGstValidateMediaGstValidateMediaTagNode *)
              tmptag->data, taglist)) {
        GST_DEBUG ("Tag already in... not adding again %" GST_PTR_FORMAT,
            taglist);
        return TRUE;
      }
    }
  }

  tagnode =
      g_slice_new0 (GstValidateMediaGstValidateMediaGstValidateMediaTagNode);
  tagnode->taglist = gst_tag_list_copy (taglist);
  str_str = gst_tag_list_to_string (tagnode->taglist);
  tagnode->str_open =
      g_markup_printf_escaped ("<tag content=\"%s\"/>", str_str);
  tagsnode->tags = g_list_prepend (tagsnode->tags, tagnode);

  g_free (str_str);

  return FALSE;
}

gboolean
gst_validate_media_descriptor_writer_add_frame (GstValidateMediaDescriptorWriter
    * writer, GstPad * pad, GstBuffer * buf)
{
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
      * streamnode;
  GstMapInfo map;
  gchar *checksum;
  guint id;
  GstValidateMediaGstValidateMediaGstValidateMediaFrameNode *fnode;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (((GstValidateMediaDescriptor *) writer)->filenode,
      FALSE);

  ((GstValidateMediaDescriptor *) writer)->filenode->frame_detection = TRUE;
  GST_VALIDATE_MEDIA_DESCRIPTOR_LOCK (writer);
  streamnode =
      gst_validate_media_descriptor_find_stream_node_by_pad (
      (GstValidateMediaDescriptor *)
      writer, pad);
  if (streamnode == NULL) {
    GST_VALIDATE_MEDIA_DESCRIPTOR_UNLOCK (writer);
    return FALSE;
  }

  id = g_list_length (streamnode->frames);
  fnode =
      g_slice_new0 (GstValidateMediaGstValidateMediaGstValidateMediaFrameNode);

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
  fnode->running_time =
      gst_segment_to_running_time (&streamnode->segment, GST_FORMAT_TIME,
      GST_BUFFER_PTS (buf));
  fnode->is_keyframe =
      (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT) == FALSE);

  fnode->str_open =
      g_markup_printf_escaped (" <frame duration=\"%" G_GUINT64_FORMAT
      "\" id=\"%i\" is-keyframe=\"%s\" offset=\"%" G_GUINT64_FORMAT
      "\" offset-end=\"%" G_GUINT64_FORMAT "\" pts=\"%" G_GUINT64_FORMAT
      "\"  dts=\"%" G_GUINT64_FORMAT "\" running-time=\"%" G_GUINT64_FORMAT
      "\" checksum=\"%s\"/>",
      fnode->duration, id, fnode->is_keyframe ? "true" : "false",
      fnode->offset, fnode->offset_end, fnode->pts, fnode->dts,
      fnode->running_time, checksum);

  fnode->str_close = NULL;

  streamnode->frames = g_list_append (streamnode->frames, fnode);

  g_free (checksum);
  GST_VALIDATE_MEDIA_DESCRIPTOR_UNLOCK (writer);

  return TRUE;
}

gboolean
gst_validate_media_descriptor_writer_write (GstValidateMediaDescriptorWriter *
    writer, const gchar * filename)
{
  gboolean ret = FALSE;
  gchar *serialized;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (((GstValidateMediaDescriptor *) writer)->filenode,
      FALSE);

  serialized = serialize_filenode (writer);


  if (g_file_set_contents (filename, serialized, -1, NULL) == TRUE)
    ret = TRUE;


  g_free (serialized);

  return ret;
}

gchar *
gst_validate_media_descriptor_writer_serialize (GstValidateMediaDescriptorWriter
    * writer)
{
  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (((GstValidateMediaDescriptor *) writer)->filenode,
      FALSE);

  return serialize_filenode (writer);
}
