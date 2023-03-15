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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/validate/validate.h>
#include "media-descriptor-writer.h"
#include <string.h>

#include "gst-validate-internal.h"

struct _GstValidateMediaDescriptorWriterPrivate
{
  GstElement *pipeline;
  GstCaps *raw_caps;
  GMainLoop *loop;

  GList *parsers;
  GstValidateMediaDescriptorWriterFlags flags;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstValidateMediaDescriptorWriter,
    gst_validate_media_descriptor_writer, GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR);

#define STR_APPEND(arg, nb_white)  \
  g_string_append_printf (res, "%*s%s%s", (nb_white), " ", (arg), "\n"); \

#define STR_APPEND0(arg) STR_APPEND((arg), 0)
#define STR_APPEND1(arg) STR_APPEND((arg), 2)
#define STR_APPEND2(arg) STR_APPEND((arg), 4)
#define STR_APPEND3(arg) STR_APPEND((arg), 6)
#define STR_APPEND4(arg) STR_APPEND((arg), 8)

#define FLAG_IS_SET(writer,flag)       ((writer->priv->flags & (flag)) == (flag))

enum
{
  PROP_0,
  PROP_PATH,
  N_PROPERTIES
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


  writer->priv = priv =
      gst_validate_media_descriptor_writer_get_instance_private (writer);

  writer->priv->parsers =
      gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_PARSER,
      GST_RANK_MARGINAL);
}

static void
    gst_validate_media_descriptor_writer_class_init
    (GstValidateMediaDescriptorWriterClass * self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

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
  GstValidateMediaTagsNode *tagsnode;
  GstValidateMediaFileNode
      * filenode =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
          *) writer);

  tmpstr = g_markup_printf_escaped ("<file duration=\"%" G_GUINT64_FORMAT
      "\" frame-detection=\"%i\" skip-parsers=\"%i\" uri=\"%s\" seekable=\"%s\">\n",
      filenode->duration, filenode->frame_detection, filenode->skip_parsers,
      filenode->uri, filenode->seekable ? "true" : "false");

  if (filenode->caps)
    caps_str = gst_caps_to_string (filenode->caps);
  else
    caps_str = g_strdup ("");

  res = g_string_new (tmpstr);
  g_free (tmpstr);
  tmpstr = g_markup_printf_escaped ("  <streams caps=\"%s\">\n", caps_str);
  g_string_append (res, tmpstr);
  g_free (tmpstr);
  g_free (caps_str);
  for (tmp = filenode->streams; tmp; tmp = tmp->next) {
    GList *tmp3;
    GstValidateMediaStreamNode
        * snode = ((GstValidateMediaStreamNode *) tmp->data);


    STR_APPEND2 (snode->str_open);

    /* Segment are always prepended, let's bring them back to the right order */
    STR_APPEND3 ("<segments>");
    for (tmp2 = snode->segments; tmp2; tmp2 = tmp2->next)
      STR_APPEND4 (((GstValidateSegmentNode *) tmp2->data)->str_open);
    STR_APPEND3 ("</segments>");

    for (tmp2 = snode->frames; tmp2; tmp2 = tmp2->next) {
      STR_APPEND3 (((GstValidateMediaFrameNode *) tmp2->data)->str_open);
    }

    tagsnode = snode->tags;
    if (tagsnode) {
      STR_APPEND3 (tagsnode->str_open);
      for (tmp3 = tagsnode->tags; tmp3; tmp3 = tmp3->next) {
        STR_APPEND4 (((GstValidateMediaTagNode *) tmp3->data)->str_open);
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
      STR_APPEND2 (((GstValidateMediaTagNode *)
              tmp2->data)->str_open);
    }
    STR_APPEND1 (tagsnode->str_close);
  }

  g_string_append (res, filenode->str_close);

  return g_string_free (res, FALSE);
}

/* Should be called with GST_VALIDATE_MEDIA_DESCRIPTOR_LOCK */
static
    GstValidateMediaStreamNode
    * gst_validate_media_descriptor_find_stream_node_by_pad
    (GstValidateMediaDescriptor * md, GstPad * pad)
{
  GList *tmp;

  for (tmp = gst_validate_media_descriptor_get_file_node (md)->streams; tmp;
      tmp = tmp->next) {
    GstValidateMediaStreamNode *streamnode =
        (GstValidateMediaStreamNode *) tmp->data;

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
  GstValidateMediaFileNode *fnode;

  writer =
      g_object_new (GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR_WRITER,
      "validate-runner", runner, NULL);

  fnode =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
          *) writer);
  fnode->uri = g_strdup (uri);
  fnode->duration = duration;
  fnode->seekable = seekable;
  fnode->str_open = NULL;

  fnode->str_close = g_markup_printf_escaped ("</file>");

  return writer;
}

static GstCaps *
strip_caps_to_avoid_parsers (GstValidateMediaDescriptorWriter * writer,
    GstCaps * caps)
{
  gint i;
  GstStructure *structure, *new_struct;
  GstCaps *stripped;

  /* If parsers are wanted, use exactly the caps reported by the discoverer (which also
   * plugs parsers). */
  if (!FLAG_IS_SET (writer,
          GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_NO_PARSER))
    return gst_caps_copy (caps);

  /* Otherwise use the simplest version of those caps (with the names only),
   * meaning that decodebin will never plug any parser */
  stripped = gst_caps_new_empty ();
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);
    new_struct = gst_structure_new_empty (gst_structure_get_name (structure));

    gst_caps_append_structure (stripped, new_struct);
  }

  return stripped;
}

static gboolean
    gst_validate_media_descriptor_writer_add_stream
    (GstValidateMediaDescriptorWriter * writer, GstDiscovererStreamInfo * info)
{
  const gchar *stype;
  gboolean ret = FALSE;
  GstCaps *caps;
  gchar *capsstr = NULL;
  GstValidateMediaStreamNode *snode = NULL;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer), FALSE);

  snode = g_new0 (GstValidateMediaStreamNode, 1);
  snode->frames = NULL;
  snode->cframe = NULL;

  snode->id = g_strdup (gst_discoverer_stream_info_get_stream_id (info));
  if (snode->id == NULL) {
    caps = gst_discoverer_stream_info_get_caps (info);
    capsstr = gst_caps_to_string (caps);

    g_free (snode);
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

  gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor *)
      writer)->streams =
      g_list_prepend (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer)->streams, snode);

  if (gst_discoverer_stream_info_get_tags (info)) {
    gst_validate_media_descriptor_writer_add_tags (writer, snode->id,
        gst_discoverer_stream_info_get_tags (info));
  }

  if (writer->priv->raw_caps == NULL)
    writer->priv->raw_caps = strip_caps_to_avoid_parsers (writer, caps);
  else {
    writer->priv->raw_caps = gst_caps_merge (writer->priv->raw_caps,
        strip_caps_to_avoid_parsers (writer, caps));
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
        GstValidateMediaStreamNode *streamnode;

        streamnode =
            gst_validate_media_descriptor_find_stream_node_by_pad (
            (GstValidateMediaDescriptor *)
            writer, pad);
        if (streamnode) {
          GstValidateSegmentNode *segment_node =
              g_new0 (GstValidateSegmentNode, 1);

          gst_event_parse_segment (event, &segment);
          gst_segment_copy_into (segment, &segment_node->segment);
          segment_node->next_frame_id = g_list_length (streamnode->frames);

          segment_node->str_open =
              g_markup_printf_escaped ("<segment next-frame-id=\"%d\""
              " flags=\"%d\" rate=\"%f\" applied-rate=\"%f\""
              " format=\"%d\" base=\"%" G_GUINT64_FORMAT "\" offset=\"%"
              G_GUINT64_FORMAT "\" start=\"%" G_GUINT64_FORMAT "\""
              " stop=\"%" G_GUINT64_FORMAT "\" time=\"%" G_GUINT64_FORMAT
              "\" position=\"%" G_GUINT64_FORMAT "\" duration=\"%"
              G_GUINT64_FORMAT "\"/>", segment_node->next_frame_id,
              segment->flags, segment->rate, segment->applied_rate,
              segment->format, segment->base, segment->offset, segment->start,
              segment->stop, segment->time, segment->position,
              segment->duration);

          streamnode->segments =
              g_list_prepend (streamnode->segments, segment_node);
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
    GstValidateMediaStreamNode *snode = NULL;
    const gchar *stream_id;

    gst_event_parse_stream_start (*event, &stream_id);
    for (tmp =
        gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
                *) writer)->streams; tmp; tmp = tmp->next) {
      GstValidateMediaStreamNode *subnode =
          (GstValidateMediaStreamNode *) tmp->data;
      if (g_strcmp0 (subnode->id, stream_id) == 0) {
        snode = subnode;

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

  if (FLAG_IS_SET (writer,
          GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_NO_PARSER))
    return NULL;

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
  GstValidateMediaStreamNode *snode = NULL;
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
    gst_object_unref (sinkpad);

    srcpad = gst_element_get_static_pad (parser, "src");
  } else {
    srcpad = gst_object_ref (pad);
  }

  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  gst_bin_add (GST_BIN (writer->priv->pipeline), fakesink);
  gst_element_sync_state_with_parent (fakesink);
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (sinkpad);
  gst_pad_sticky_events_foreach (pad,
      (GstPadStickyEventsForeachFunction) _find_stream_id, writer);

  if (srcpad != pad) {
    snode =
        gst_validate_media_descriptor_find_stream_node_by_pad (
        (GstValidateMediaDescriptor *)
        writer, pad);
    if (snode) {
      gst_object_unref (snode->pad);
      snode->pad = gst_object_ref (srcpad);
    }
  }

  gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) _uridecodebin_probe, writer, NULL);

  gst_object_unref (srcpad);
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
  GList *tmp;
  GstStateChangeReturn sret;
  GstValidateMonitor *monitor;
  GstValidateMediaFileNode *filenode;

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
      gst_validate_printf (NULL, "Pipeline failed to go to PLAYING state\n");
      return FALSE;
    default:
      break;
  }

  g_main_loop_run (writer->priv->loop);

  filenode =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
          *) writer);
  /* Segment are always prepended, let's reorder them. */
  for (tmp = filenode->streams; tmp; tmp = tmp->next) {
    GstValidateMediaStreamNode
        * snode = ((GstValidateMediaStreamNode *) tmp->data);
    snode->segments = g_list_reverse (snode->segments);
  }

  gst_element_set_state (writer->priv->pipeline, GST_STATE_NULL);
  gst_object_unref (writer->priv->pipeline);
  writer->priv->pipeline = NULL;
  g_main_loop_unref (writer->priv->loop);
  writer->priv->loop = NULL;
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_validate_reporter_purge_reports (GST_VALIDATE_REPORTER (monitor));
  g_object_unref (monitor);

  return TRUE;
}

GstValidateMediaDescriptorWriter *
gst_validate_media_descriptor_writer_new_discover (GstValidateRunner * runner,
    const gchar * uri, GstValidateMediaDescriptorWriterFlags flags,
    GError ** err)
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

    writer->priv->flags = flags;
    if (FLAG_IS_SET (writer,
            GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_HANDLE_GLOGS))
      gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (writer));

    tags = gst_discoverer_info_get_tags (info);
    if (tags)
      gst_validate_media_descriptor_writer_add_taglist (writer, tags);

    if (GST_IS_DISCOVERER_CONTAINER_INFO (streaminfo)) {
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
              *) writer)->caps =
          gst_discoverer_stream_info_get_caps (GST_DISCOVERER_STREAM_INFO
          (streaminfo));

      streams = gst_discoverer_info_get_stream_list (info);
      for (tmp = streams; tmp; tmp = tmp->next) {
        GstDiscovererStreamInfo *streaminfo =
            (GstDiscovererStreamInfo *) tmp->data;
        gst_validate_media_descriptor_writer_add_stream (writer, streaminfo);
      }
    } else {
      GstDiscovererStreamInfo *nextinfo;
      if (!GST_IS_DISCOVERER_AUDIO_INFO (info)
          && !GST_IS_DISCOVERER_VIDEO_INFO (info)) {
        nextinfo = gst_discoverer_stream_info_get_next (streaminfo);
        if (nextinfo) {
          GstValidateMediaFileNode *fn =
              gst_validate_media_descriptor_get_file_node (
              (GstValidateMediaDescriptor *) writer);
          fn->caps = gst_discoverer_stream_info_get_caps (streaminfo);
          gst_discoverer_stream_info_unref (streaminfo);
          streaminfo = nextinfo;
        }
      }
      do {
        gst_validate_media_descriptor_writer_add_stream (writer, streaminfo);
        nextinfo = gst_discoverer_stream_info_get_next (streaminfo);
        gst_discoverer_stream_info_unref (streaminfo);
        streaminfo = nextinfo;
      } while (streaminfo);
    }
  } else {
    GST_VALIDATE_REPORT (writer, FILE_NO_STREAM_INFO,
        "Discoverer info, does not contain the stream info");
    goto out;
  }

  media_descriptor = (GstValidateMediaDescriptor *) writer;
  if (streams == NULL
      && gst_validate_media_descriptor_get_file_node (media_descriptor)->caps)
    writer->priv->raw_caps =
        gst_caps_copy (gst_validate_media_descriptor_get_file_node
        (media_descriptor)->caps);

  gst_discoverer_stream_info_list_free (streams);


  if (FLAG_IS_SET (writer, GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_FULL))
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
  GstValidateMediaTagsNode *tagsnode;
  GstValidateMediaTagNode *tagnode;
  GList *tmp, *tmptag;

  gchar *str_str = NULL;
  GstValidateMediaStreamNode *snode = NULL;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer), FALSE);

  for (tmp =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
              *) writer)->streams; tmp; tmp = tmp->next) {
    GstValidateMediaStreamNode *subnode =
        (GstValidateMediaStreamNode *) tmp->data;
    if (g_strcmp0 (subnode->id, stream_id) == 0) {
      snode = subnode;

      break;
    }
  }

  if (snode == NULL) {
    GST_WARNING ("Could not find stream with id: %s", stream_id);

    return FALSE;
  }

  if (snode->tags == NULL) {
    tagsnode = g_new0 (GstValidateMediaTagsNode, 1);
    tagsnode->str_open = g_markup_printf_escaped ("<tags>");
    tagsnode->str_close = g_markup_printf_escaped ("</tags>");
    snode->tags = tagsnode;
  } else {
    tagsnode = snode->tags;

    for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
      if (gst_validate_tag_node_compare ((GstValidateMediaTagNode *)
              tmptag->data, taglist)) {
        GST_DEBUG ("Tag already in... not adding again %" GST_PTR_FORMAT,
            taglist);
        return TRUE;
      }
    }
  }

  tagnode = g_new0 (GstValidateMediaTagNode, 1);
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
  GstValidateMediaStreamNode *snode = NULL;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer), FALSE);

  caps = gst_pad_get_current_caps (pad);
  for (tmp =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
              *) writer)->streams; tmp; tmp = tmp->next) {
    GstValidateMediaStreamNode *streamnode =
        (GstValidateMediaStreamNode *) tmp->data;

    if (streamnode->pad == pad) {
      goto done;
    }
  }

  snode = g_new0 (GstValidateMediaStreamNode, 1);
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

  gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor *)
      writer)->streams =
      g_list_prepend (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer)->streams, snode);

done:
  if (caps != NULL)
    gst_caps_unref (caps);
  g_free (capsstr);
  g_free (padname);

  return ret;
}

gboolean
    gst_validate_media_descriptor_writer_add_taglist
    (GstValidateMediaDescriptorWriter * writer, const GstTagList * taglist)
{
  gchar *str_str = NULL;
  GstValidateMediaTagsNode *tagsnode;
  GstValidateMediaTagNode *tagnode;
  GList *tmptag;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer), FALSE);

  if (gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
              *) writer)->tags == NULL) {
    tagsnode = g_new0 (GstValidateMediaTagsNode, 1);
    tagsnode->str_open = g_markup_printf_escaped ("<tags>");
    tagsnode->str_close = g_markup_printf_escaped ("</tags>");
    gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor *)
        writer)->tags = tagsnode;
  } else {
    tagsnode =
        gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
            *) writer)->tags;
    for (tmptag = tagsnode->tags; tmptag; tmptag = tmptag->next) {
      if (gst_validate_tag_node_compare ((GstValidateMediaTagNode *)
              tmptag->data, taglist)) {
        GST_DEBUG ("Tag already in... not adding again %" GST_PTR_FORMAT,
            taglist);
        return TRUE;
      }
    }
  }

  tagnode = g_new0 (GstValidateMediaTagNode, 1);
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
  GstValidateMediaStreamNode *streamnode;
  GstMapInfo map;
  gchar *checksum;
  guint id;
  GstSegment *segment;
  GstValidateMediaFrameNode *fnode;
  GstValidateMediaFileNode *filenode;

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR_WRITER (writer),
      FALSE);
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer), FALSE);

  filenode =
      gst_validate_media_descriptor_get_file_node ((GstValidateMediaDescriptor
          *) writer);
  filenode->frame_detection = TRUE;
  filenode->skip_parsers =
      FLAG_IS_SET (writer,
      GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_NO_PARSER);
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
  fnode = g_new0 (GstValidateMediaFrameNode, 1);

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

  g_assert (streamnode->segments);
  segment = &((GstValidateSegmentNode *) streamnode->segments->data)->segment;
  fnode->running_time =
      gst_segment_to_running_time (segment, GST_FORMAT_TIME,
      GST_BUFFER_PTS (buf));
  fnode->is_keyframe =
      (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT) == FALSE);

  fnode->str_open =
      g_markup_printf_escaped (" <frame duration=\"%" G_GUINT64_FORMAT
      "\" id=\"%i\" is-keyframe=\"%s\" offset=\"%" G_GUINT64_FORMAT
      "\" offset-end=\"%" G_GUINT64_FORMAT "\" pts=\"%" G_GUINT64_FORMAT
      "\" dts=\"%" G_GUINT64_FORMAT "\" running-time=\"%" G_GUINT64_FORMAT
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
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer), FALSE);

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
  g_return_val_if_fail (gst_validate_media_descriptor_get_file_node (
          (GstValidateMediaDescriptor *) writer), FALSE);

  return serialize_filenode (writer);
}
