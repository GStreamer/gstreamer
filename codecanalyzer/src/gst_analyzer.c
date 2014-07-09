/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
/* SECTION: gst-analyzer
 * gst-analyzer is the back-end code of codecanalyzer
 * which is for activating the whole gstreamer pipeline.
 * The usual pipeline contains three gstreamer elements,
 * a src(filesrc), parser(any video parser element supporing
 * by the codecanalyzer and upstream gstreamer) and an
 * analyzersink which is residing in plugins/gst/analzyersink.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gst_analyzer.h"
#include <analyzer_utils.h>

typedef struct
{
  const gint ret;
  const gchar *name;
} GstAnalyzerStatusInfo;

static GstAnalyzerStatusInfo analyzer_status_info[] = {
  {GST_ANALYZER_STATUS_SUCCESS, "Success"},
  {GST_ANALYZER_STATUS_CODEC_PARSER_MISSING, "Codec Parser is missing"},
  {GST_ANALYZER_STATUS_CODEC_NOT_SUPPORTED, "Codec not supported"},
  {GST_ANALYZER_STATUS_STREAM_FORMAT_UNKNOWN, "Unknown stream format"},
  {GST_ANALYZER_STATUS_ERROR_UNKNOWN, "Failed to start the gstreamer engine"}
};

const gchar *
gst_analyzer_status_get_name (GstAnalyzerStatus status)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (analyzer_status_info); i++) {
    if (status == analyzer_status_info[i].ret)
      return analyzer_status_info[i].name;
  }
  return "unknown";
}

typedef struct
{
  const gchar *discoverer_codec_name;
  const gchar *codec_short_name;
  GstAnalyzerCodecType codec_type;
  gchar *parser_name;
} CodecInfo;

static const CodecInfo codecs_info[] = {

  {"MPEG-2 Video", "mpeg2", GST_ANALYZER_CODEC_MPEG2_VIDEO, "mpegvideoparse"},
  {"H.264", "h264", GST_ANALYZER_CODEC_H264, "h264parse"},
  {"H.265", "h265", GST_ANALYZER_CODEC_H265, "h265parse"},
  {"UNKNOWN", "unknown", GST_ANALYZER_CODEC_UNKNOWN, NULL}
};

static const CodecInfo *
find_codec_info (gchar * name)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS (codecs_info); ++i) {
    if (!strcmp (name, codecs_info[i].discoverer_codec_name))
      return &codecs_info[i];
  }
  return NULL;
}

void
gst_analyzer_video_info_destroy (GstAnalyzerVideoInfo * analyzer_vinfo)
{
  g_return_if_fail (analyzer_vinfo != NULL);

  if (analyzer_vinfo->codec_name)
    g_free (analyzer_vinfo->codec_name);

  g_slice_free (GstAnalyzerVideoInfo, analyzer_vinfo);
}

gboolean
gst_analyzer_video_info_from_uri (GstAnalyzerVideoInfo * analyzer_vinfo,
    gchar * uri)
{
  g_return_val_if_fail (analyzer_vinfo != NULL, FALSE);
  g_return_val_if_fail (uri != NULL, FALSE);

  GstDiscoverer *discoverer = NULL;
  GstDiscovererInfo *d_info = NULL;
  GstDiscovererVideoInfo *dv_info = NULL;
  GList *list = NULL;
  GstCaps *caps = NULL;

  discoverer = gst_discoverer_new (3 * GST_SECOND, NULL);
  g_return_val_if_fail (discoverer != NULL, FALSE);

  d_info = gst_discoverer_discover_uri (discoverer, uri, NULL);
  g_return_val_if_fail (d_info != NULL, FALSE);

  list = gst_discoverer_info_get_video_streams (d_info);
  g_return_val_if_fail (list != NULL && list->data != NULL, FALSE);

  caps = gst_discoverer_stream_info_get_caps ((GstDiscovererStreamInfo *)
      list->data);
  dv_info = (GstDiscovererVideoInfo *) list->data;

  if (caps)
    analyzer_vinfo->codec_name = gst_pb_utils_get_codec_description (caps);

  analyzer_vinfo->width = gst_discoverer_video_info_get_width (dv_info);
  analyzer_vinfo->height = gst_discoverer_video_info_get_height (dv_info);
  analyzer_vinfo->depth = gst_discoverer_video_info_get_depth (dv_info);
  analyzer_vinfo->avg_bitrate = gst_discoverer_video_info_get_bitrate (dv_info);
  analyzer_vinfo->max_bitrate =
      gst_discoverer_video_info_get_max_bitrate (dv_info);
  analyzer_vinfo->fps_n = gst_discoverer_video_info_get_framerate_num (dv_info);
  analyzer_vinfo->fps_d =
      gst_discoverer_video_info_get_framerate_denom (dv_info);
  analyzer_vinfo->par_n = gst_discoverer_video_info_get_par_num (dv_info);
  analyzer_vinfo->par_d = gst_discoverer_video_info_get_par_denom (dv_info);

  gst_caps_unref (caps);
  gst_discoverer_stream_info_list_free (list);
  g_object_unref (discoverer);

  g_debug
      ("codec=%s w=%d h=%d d=%d avg_bitrate=%d max_bitrate=%d fps_n=%d fps_d=%d par_n=%d par_d=%d \n",
      analyzer_vinfo->codec_name, analyzer_vinfo->width, analyzer_vinfo->height,
      analyzer_vinfo->depth, analyzer_vinfo->avg_bitrate,
      analyzer_vinfo->max_bitrate, analyzer_vinfo->fps_n, analyzer_vinfo->fps_d,
      analyzer_vinfo->par_n, analyzer_vinfo->par_d);

  return TRUE;
}

GstAnalyzerVideoInfo *
gst_analyzer_video_info_new ()
{
  GstAnalyzerVideoInfo *vinfo;

  vinfo = g_slice_new0 (GstAnalyzerVideoInfo);

  return vinfo;
}

static void
new_frame_callback (GstElement * element, GstBuffer * buffer, gint frame_num,
    gpointer data)
{
  GstAnalyzer *analyzer = (GstAnalyzer *) data;

  analyzer->NumOfAnalyzedFrames = frame_num + 1;

  if (analyzer->NumOfAnalyzedFrames == analyzer->NumOfFramesToAnalyze)
    analyzer->complete_analyze = TRUE;
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GstAnalyzer *analyzer = (GstAnalyzer *) data;

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS) {
    g_printf ("<===Received EOS: All frames are analyzed====> \n");
    analyzer->complete_analyze = TRUE;
    return FALSE;
  } else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    GError *error = NULL;
    gst_message_parse_error (message, &error, NULL);
    g_error ("gstreamer error : %s", error->message);
    g_error_free (error);
    return FALSE;
  }
  return TRUE;
}

void
gst_analyzer_destroy (GstAnalyzer * analyzer)
{
  if (analyzer) {
    if (analyzer->video_info)
      gst_analyzer_video_info_destroy (analyzer->video_info);

    if (analyzer->codec_name)
      g_free (analyzer->codec_name);

    gst_analyzer_stop (analyzer);
    g_slice_free (GstAnalyzer, analyzer);
  }
}

void
gst_analyzer_set_file_name (GstAnalyzer * analyzer, char *uri)
{
  g_object_set (G_OBJECT (analyzer->src), "location", uri, NULL);
}

void
gst_analyzer_set_destination_dir_path (GstAnalyzer * analyzer, char *path)
{
  g_object_set (G_OBJECT (analyzer->sink), "location", path, NULL);
  g_debug ("Destination for xml_files and hex_files %s ", path);
}

void
gst_analyzer_set_num_frames (GstAnalyzer * analyzer, gint frame_count)
{
  g_object_set (G_OBJECT (analyzer->sink), "num-frames", frame_count, NULL);
  analyzer->NumOfFramesToAnalyze = frame_count;
}

gboolean
gst_analyzer_stop (GstAnalyzer * analyzer)
{
  if (analyzer->pipeline)
    gst_element_set_state (analyzer->pipeline, GST_STATE_NULL);

  if (analyzer->bus_watch_id)
    g_source_remove (analyzer->bus_watch_id);

  return TRUE;
}

gboolean
gst_analyzer_start (GstAnalyzer * analyzer)
{
  gst_element_set_state (analyzer->pipeline, GST_STATE_PLAYING);
  return TRUE;
}

GstAnalyzerStatus
gst_analyzer_init (GstAnalyzer * analyzer, char *uri)
{
  GstAnalyzerStatus status = GST_ANALYZER_STATUS_ERROR_UNKNOWN;
  const CodecInfo *codec_info;
  GstBus *bus;
  gboolean ret;

  analyzer->NumOfAnalyzedFrames = 0;
  analyzer->complete_analyze = FALSE;
  analyzer->NumOfFramesToAnalyze = -1;

  if (!gst_is_initialized ())
    gst_init (NULL, NULL);

  if (!analyzer_sink_register_static ()) {
    g_error ("Failed to register static plugins.... \n");
    status = GST_ANALYZER_STATUS_CODEC_PARSER_MISSING;
    goto error;
  }

  /* GstDiscoverer to extract general stream info */
  analyzer->video_info = gst_analyzer_video_info_new ();
  ret = gst_analyzer_video_info_from_uri (analyzer->video_info, uri);

  if (ret && analyzer->video_info->codec_name)
    codec_info = find_codec_info (analyzer->video_info->codec_name);

  if (!ret || codec_info == NULL
      || codec_info->codec_type == GST_ANALYZER_CODEC_UNKNOWN) {
    status = GST_ANALYZER_STATUS_STREAM_FORMAT_UNKNOWN;
    goto error;
  }

  if (ret && codec_info) {
    switch (codec_info->codec_type) {
      case GST_ANALYZER_CODEC_MPEG2_VIDEO:
        status = GST_ANALYZER_STATUS_SUCCESS;
        break;
      default:
        status = GST_ANALYZER_STATUS_CODEC_NOT_SUPPORTED;
        goto error;
        break;
    }
  }
  analyzer->codec_name = g_strdup (codec_info->codec_short_name);

  analyzer->src = gst_element_factory_make ("filesrc", "file-src");
  analyzer->parser =
      gst_element_factory_make (codec_info->parser_name,
      "codec-analyzer-video-parse");
  analyzer->sink = gst_element_factory_make ("analyzersink", "sink");
  analyzer->pipeline = gst_pipeline_new ("pipeline");

  if (!analyzer->src || !analyzer->parser || !analyzer->sink) {
    g_error ("Failed to create the necessary gstreamer elements..");
    status = GST_ANALYZER_STATUS_ERROR_UNKNOWN;
    goto error;
  }

  g_signal_connect (analyzer->sink, "new-frame", (GCallback) new_frame_callback,
      analyzer);
  if (!strcmp (analyzer->codec_name, "mpeg2"))
    g_object_set (G_OBJECT (analyzer->parser), "drop", FALSE, NULL);

  gst_bin_add_many (GST_BIN (analyzer->pipeline), analyzer->src,
      analyzer->parser, analyzer->sink, NULL);
  gst_element_link_many (analyzer->src, analyzer->parser, analyzer->sink, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (analyzer->pipeline));
  analyzer->bus_watch_id = gst_bus_add_watch (bus, bus_callback, analyzer);
  gst_object_unref (GST_OBJECT (bus));

  return status;

error:
  return status;
}
