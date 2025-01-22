/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include <gst/codecparsers/gsth264parser.h>
#include <string.h>

typedef struct
{
  GstH264NalParser *parser;
  guint nalu_len_size;
  gboolean drop_p;
} ParserData;

static void
parser_data_free (ParserData * data)
{
  gst_h264_nal_parser_free (data->parser);
  g_free (data);
}

static GstH264ParserResult
handle_nalu (GstH264NalParser * parser, GstH264NalUnit * nalu,
    gboolean * is_pframe, gboolean * is_bframe)
{
  GstH264ParserResult ret = GST_H264_PARSER_OK;
  GstH264SliceHdr slice;

  switch (nalu->type) {
    case GST_H264_NAL_SPS:
    case GST_H264_NAL_PPS:
      /* Stores SPS and PPS, required to parse slice */
      ret = gst_h264_parser_parse_nal (parser, nalu);
      break;
    case GST_H264_NAL_SLICE_IDR:
      /* slice indicates IDR already, do not need to parse slice */
      break;
    case GST_H264_NAL_SLICE:
    case GST_H264_NAL_SLICE_DPA:
    case GST_H264_NAL_SLICE_DPB:
    case GST_H264_NAL_SLICE_DPC:
    case GST_H264_NAL_SLICE_EXT:
      /* To detect frame type, we should parse slice header */
      ret = gst_h264_parser_parse_slice_hdr (parser, nalu, &slice,
          FALSE, FALSE);
      if (ret == GST_H264_PARSER_OK) {
        if (GST_H264_IS_B_SLICE (&slice))
          *is_bframe = TRUE;

        if (GST_H264_IS_P_SLICE (&slice))
          *is_pframe = TRUE;
      }
      break;
    default:
      break;
  }

  return ret;
}

static GstPadProbeReturn
parse_src_probe_bytestream (GstPad * pad, GstPadProbeInfo * info,
    ParserData * data)
{
  GstH264NalParser *parser = data->parser;
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  GstMapInfo map;
  GstH264ParserResult ret = GST_H264_PARSER_OK;
  GstH264NalUnit nalu;
  gboolean is_bframe = FALSE;
  gboolean is_pframe = FALSE;

  memset (&nalu, 0, sizeof (nalu));

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  ret = gst_h264_parser_identify_nalu (parser, map.data, 0, map.size, &nalu);

  /* This is a case where last nalu in bytestream AU,
   * since there's no next startcode. This is expected and not an error */
  if (ret == GST_H264_PARSER_NO_NAL_END)
    ret = GST_H264_PARSER_OK;

  while (ret == GST_H264_PARSER_OK) {
    ret = handle_nalu (parser, &nalu, &is_pframe, &is_bframe);

    /* Prepare to parse next nalu if any */
    if (ret == GST_H264_PARSER_OK) {
      ret = gst_h264_parser_identify_nalu (parser, map.data,
          nalu.offset + nalu.size, map.size, &nalu);
    }

    /* Again, this is expected case and not an error */
    if (ret == GST_H264_PARSER_NO_NAL_END)
      ret = GST_H264_PARSER_OK;
  }

  gst_buffer_unmap (buffer, &map);

  if (is_bframe) {
    gst_println ("Dropping bframe %" GST_PTR_FORMAT, buffer);
    return GST_PAD_PROBE_DROP;
  }

  if (is_pframe && data->drop_p) {
    gst_println ("Dropping P frame %" GST_PTR_FORMAT, buffer);
    return GST_PAD_PROBE_DROP;
  }

  return GST_PAD_PROBE_OK;
}

static void
parse_codec_data (ParserData * data, GstMapInfo * map)
{
  GstH264DecoderConfigRecord *config = NULL;
  GstH264NalUnit *nalu;
  guint i;
  GstH264ParserResult ret;

  ret = gst_h264_parser_parse_decoder_config_record (data->parser,
      map->data, map->size, &config);
  if (ret != GST_H264_PARSER_OK) {
    gst_printerrln ("Couldn't parse codec data");
    return;
  }

  data->nalu_len_size = config->length_size_minus_one + 1;
  for (i = 0; i < config->sps->len; i++) {
    GstH264SPS sps;

    nalu = &g_array_index (config->sps, GstH264NalUnit, i);

    if (nalu->type != GST_H264_NAL_SPS)
      continue;

    ret = gst_h264_parser_parse_sps (data->parser, nalu, &sps);
    if (ret != GST_H264_PARSER_OK) {
      gst_printerrln ("Couldn't parse SPS");
      goto out;
    }

    gst_h264_sps_clear (&sps);
  }

  for (i = 0; i < config->pps->len; i++) {
    GstH264PPS pps;

    nalu = &g_array_index (config->pps, GstH264NalUnit, i);
    if (nalu->type != GST_H264_NAL_PPS)
      continue;

    ret = gst_h264_parser_parse_pps (data->parser, nalu, &pps);

    if (ret != GST_H264_PARSER_OK) {
      gst_printerrln ("Couldn't parse SPS");
      goto out;
    }

    gst_h264_pps_clear (&pps);
  }

out:
  gst_h264_decoder_config_record_free (config);
}

static GstPadProbeReturn
parse_src_probe_avc (GstPad * pad, GstPadProbeInfo * info, ParserData * data)
{
  GstH264NalParser *parser = data->parser;
  GstBuffer *buffer;
  GstMapInfo map;
  GstH264ParserResult ret = GST_H264_PARSER_OK;
  GstH264NalUnit nalu;
  gboolean is_bframe = FALSE;
  gboolean is_pframe = FALSE;

  /* Extract codec data and parse SPS/PPS */
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
    if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
      GstCaps *caps;
      GstStructure *s;
      const GValue *value;

      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);
      value = gst_structure_get_value (s, "codec_data");
      if (value && G_VALUE_TYPE (value) == GST_TYPE_BUFFER) {
        buffer = (GstBuffer *) g_value_dup_boxed (value);
        gst_buffer_map (buffer, &map, GST_MAP_READ);

        parse_codec_data (data, &map);
        gst_buffer_unmap (buffer, &map);
        gst_buffer_unref (buffer);
      }
    }

    return GST_PAD_PROBE_OK;
  }

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  memset (&nalu, 0, sizeof (nalu));

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  ret = gst_h264_parser_identify_nalu_avc (parser,
      map.data, 0, map.size, data->nalu_len_size, &nalu);

  while (ret == GST_H264_PARSER_OK) {
    ret = handle_nalu (parser, &nalu, &is_pframe, &is_bframe);

    /* Prepare to parse next nalu if any */
    if (ret == GST_H264_PARSER_OK) {
      ret = gst_h264_parser_identify_nalu_avc (parser, map.data,
          nalu.offset + nalu.size, map.size, data->nalu_len_size, &nalu);
    }
  }

  gst_buffer_unmap (buffer, &map);

  if (is_bframe) {
    gst_println ("Dropping B frame %" GST_PTR_FORMAT, buffer);
    return GST_PAD_PROBE_DROP;
  }

  if (is_pframe && data->drop_p) {
    gst_println ("Dropping P frame %" GST_PTR_FORMAT, buffer);
    return GST_PAD_PROBE_DROP;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
bus_handler (GstBus * bus, GstMessage * msg, GMainLoop * loop)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_printerrln ("Got ERROR");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      gst_println ("Got EOS");
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline;
  GstElement *parse;
  GError *err = NULL;
  GstPad *pad;
  GMainLoop *loop;
  ParserData *data;
  gchar *location = NULL;
  gchar *pipeline_str = NULL;
  gboolean use_avc = FALSE;
  gboolean drop_p = FALSE;
  GstBus *bus;
  guint bus_watch_id;
  GOptionEntry options[] = {
    {"use-avc", 0, 0, G_OPTION_ARG_NONE, &use_avc,
        "Use stream-format=avc instead of byte-stream", NULL}
    ,
    {"drop-p", 0, 0, G_OPTION_ARG_NONE, &drop_p, "Drop P frames", NULL}
    ,
    {"location", 0, 0, G_OPTION_ARG_STRING, &location,
        "H.264 encoded test file location", NULL},
    {NULL}
  };
  GOptionContext *option_ctx;
  gboolean ret;

  option_ctx = g_option_context_new ("GstH264Parser example");
  g_option_context_add_main_entries (option_ctx, options, NULL);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());
  ret = g_option_context_parse (option_ctx, &argc, &argv, &err);
  g_option_context_free (option_ctx);

  if (!ret) {
    gst_printerrln ("Option parsing failed: %s", err->message);
    g_clear_error (&err);
    return 1;
  }

  if (!location) {
    gst_printerrln ("Location must be specified");
    return 1;
  }

  pipeline_str = g_strdup_printf ("filesrc location=%s ! parsebin ! "
      "h264parse name=parse ! video/x-h264,stream-format=%s,alignment=au ! "
      "decodebin ! videoconvert ! autovideosink", location,
      use_avc ? "avc" : "byte-stream");

  pipeline = gst_parse_launch (pipeline_str, &err);
  g_free (pipeline_str);

  if (!pipeline) {
    gst_printerrln ("Couldn't create pipeline, error: %s", err->message);
    return 1;
  }

  data = g_new0 (ParserData, 1);
  data->parser = gst_h264_nal_parser_new ();
  data->nalu_len_size = 4;
  data->drop_p = drop_p;

  loop = g_main_loop_new (NULL, FALSE);

  parse = gst_bin_get_by_name (GST_BIN (pipeline), "parse");
  pad = gst_element_get_static_pad (parse, "src");

  if (use_avc) {
    /* In case of avc format, SPS/PPS is signalled via caps. Probe will
     * parse caps to extract SPS/PPS in addition to buffers */
    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER |
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        (GstPadProbeCallback) parse_src_probe_avc, data,
        (GDestroyNotify) parser_data_free);
  } else {
    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
        (GstPadProbeCallback) parse_src_probe_bytestream, data,
        (GDestroyNotify) parser_data_free);
  }

  gst_object_unref (parse);
  gst_object_unref (pad);

  bus = gst_element_get_bus (pipeline);
  bus_watch_id = gst_bus_add_watch (bus, (GstBusFunc) bus_handler, loop);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}
