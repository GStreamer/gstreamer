/* GStreamer
 * Copyright (C) 2012 Smart TV Alliance
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>, Collabora Ltd.
 *
 * gstmssmanifest.c:
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

#include <glib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

/* for parsing h264 codec data */
#include <gst/codecparsers/gsth264parser.h>

#include "gstmssmanifest.h"

#define DEFAULT_TIMESCALE             10000000

#define MSS_NODE_STREAM_FRAGMENT      "c"
#define MSS_NODE_STREAM_QUALITY       "QualityLevel"

#define MSS_PROP_BITRATE              "Bitrate"
#define MSS_PROP_DURATION             "d"
#define MSS_PROP_NUMBER               "n"
#define MSS_PROP_STREAM_DURATION      "Duration"
#define MSS_PROP_TIME                 "t"
#define MSS_PROP_TIMESCALE            "TimeScale"
#define MSS_PROP_URL                  "Url"

/* TODO check if atoi is successful? */

typedef struct _GstMssStreamFragment
{
  guint number;
  guint64 time;
  guint64 duration;
} GstMssStreamFragment;

struct _GstMssStream
{
  xmlNodePtr xmlnode;

  gint selectedQualityIndex;

  GList *fragments;
  GList *qualities;

  gchar *url;

  GList *current_fragment;
  GList *current_quality;

  /* TODO move this to somewhere static */
  GRegex *regex_bitrate;
  GRegex *regex_position;
};

struct _GstMssManifest
{
  xmlDocPtr xml;
  xmlNodePtr xmlrootnode;

  GSList *streams;
};

static gboolean
node_has_type (xmlNodePtr node, const gchar * name)
{
  return strcmp ((gchar *) node->name, name) == 0;
}

static void
_gst_mss_stream_init (GstMssStream * stream, xmlNodePtr node)
{
  xmlNodePtr iter;
  GstMssStreamFragment *previous_fragment = NULL;
  guint fragment_number = 0;
  guint fragment_time_accum = 0;
  GError *gerror = NULL;

  stream->xmlnode = node;

  /* get the base url path generator */
  stream->url = (gchar *) xmlGetProp (node, (xmlChar *) MSS_PROP_URL);

  for (iter = node->children; iter; iter = iter->next) {
    if (node_has_type (iter, MSS_NODE_STREAM_FRAGMENT)) {
      gchar *duration_str;
      gchar *time_str;
      gchar *seqnum_str;
      GstMssStreamFragment *fragment = g_new (GstMssStreamFragment, 1);

      duration_str = (gchar *) xmlGetProp (iter, (xmlChar *) MSS_PROP_DURATION);
      time_str = (gchar *) xmlGetProp (iter, (xmlChar *) MSS_PROP_TIME);
      seqnum_str = (gchar *) xmlGetProp (iter, (xmlChar *) MSS_PROP_NUMBER);

      /* use the node's seq number or use the previous + 1 */
      if (seqnum_str) {
        fragment->number = atoi (seqnum_str);
        g_free (seqnum_str);
      } else {
        fragment->number = fragment_number;
      }
      fragment_number = fragment->number + 1;

      if (time_str) {
        fragment->time = atoi (time_str);
        g_free (time_str);
      } else {
        fragment->time = fragment_time_accum;
      }

      /* if we have a previous fragment, means we need to set its duration */
      if (previous_fragment)
        previous_fragment->duration = fragment->time - previous_fragment->time;

      if (duration_str) {
        fragment->duration = atoi (duration_str);

        previous_fragment = NULL;
        fragment_time_accum += fragment->duration;
        g_free (duration_str);
      } else {
        /* store to set the duration at the next iteration */
        previous_fragment = fragment;
      }

      /* we reverse it later */
      stream->fragments = g_list_prepend (stream->fragments, fragment);

    } else if (node_has_type (iter, MSS_NODE_STREAM_QUALITY)) {
      stream->qualities = g_list_prepend (stream->qualities, iter);
    } else {
      /* TODO gst log this */
    }
  }

  stream->fragments = g_list_reverse (stream->fragments);
  stream->qualities = g_list_reverse (stream->qualities);

  stream->current_fragment = stream->fragments;
  stream->current_quality = stream->qualities;

  stream->regex_bitrate = g_regex_new ("\\{[Bb]itrate\\}", 0, 0, &gerror);
  stream->regex_position = g_regex_new ("\\{start[ _]time\\}", 0, 0, &gerror);
}

GstMssManifest *
gst_mss_manifest_new (const GstBuffer * data)
{
  GstMssManifest *manifest;
  xmlNodePtr root;
  xmlNodePtr nodeiter;

  manifest = g_malloc0 (sizeof (GstMssManifest));

  manifest->xml = xmlReadMemory ((const gchar *) GST_BUFFER_DATA (data),
      GST_BUFFER_SIZE (data), "manifest", NULL, 0);
  root = manifest->xmlrootnode = xmlDocGetRootElement (manifest->xml);

  for (nodeiter = root->children; nodeiter; nodeiter = nodeiter->next) {
    if (nodeiter->type == XML_ELEMENT_NODE
        && (strcmp ((const char *) nodeiter->name, "StreamIndex") == 0)) {
      GstMssStream *stream = g_new0 (GstMssStream, 1);

      manifest->streams = g_slist_append (manifest->streams, stream);
      _gst_mss_stream_init (stream, nodeiter);
    }
  }

  return manifest;
}

static void
gst_mss_stream_free (GstMssStream * stream)
{
  g_list_free_full (stream->fragments, g_free);
  g_list_free (stream->qualities);
  g_free (stream->url);
  g_regex_unref (stream->regex_position);
  g_regex_unref (stream->regex_bitrate);
  g_free (stream);
}

void
gst_mss_manifest_free (GstMssManifest * manifest)
{
  g_return_if_fail (manifest != NULL);

  g_slist_free_full (manifest->streams, (GDestroyNotify) gst_mss_stream_free);

  xmlFreeDoc (manifest->xml);
  g_free (manifest);
}

GSList *
gst_mss_manifest_get_streams (GstMssManifest * manifest)
{
  return manifest->streams;
}

GstMssStreamType
gst_mss_stream_get_type (GstMssStream * stream)
{
  gchar *prop = (gchar *) xmlGetProp (stream->xmlnode, (xmlChar *) "Type");
  GstMssStreamType ret = MSS_STREAM_TYPE_UNKNOWN;

  if (strcmp (prop, "video") == 0) {
    ret = MSS_STREAM_TYPE_VIDEO;
  } else if (strcmp (prop, "audio") == 0) {
    ret = MSS_STREAM_TYPE_AUDIO;
  }
  xmlFree (prop);
  return ret;
}

static GstCaps *
_gst_mss_stream_video_caps_from_fourcc (gchar * fourcc)
{
  if (!fourcc)
    return NULL;

  if (strcmp (fourcc, "H264") == 0) {
    return gst_caps_new_simple ("video/x-h264", "stream-format", G_TYPE_STRING,
        "avc", NULL);
  }
  return NULL;
}

static GstCaps *
_gst_mss_stream_audio_caps_from_fourcc (gchar * fourcc)
{
  if (!fourcc)
    return NULL;

  if (strcmp (fourcc, "AACL") == 0) {
    return gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 4,
        NULL);
  }
  return NULL;
}

/* copied and adapted from h264parse */
static GstBuffer *
_make_h264_codec_data (GstBuffer * sps, GstBuffer * pps)
{
  GstBuffer *buf;
  gint sps_size = 0, pps_size = 0, num_sps = 0, num_pps = 0;
  guint8 profile_idc = 0, profile_comp = 0, level_idc = 0;
  guint8 *data;
  gint nl;

  sps_size += GST_BUFFER_SIZE (sps) + 2;
  profile_idc = GST_BUFFER_DATA (sps)[1];
  profile_comp = GST_BUFFER_DATA (sps)[2];
  level_idc = GST_BUFFER_DATA (sps)[3];
  num_sps = 1;

  pps_size += GST_BUFFER_SIZE (pps) + 2;
  num_pps = 1;

  buf = gst_buffer_new_and_alloc (5 + 1 + sps_size + 1 + pps_size);
  data = GST_BUFFER_DATA (buf);
  nl = 4;

  data[0] = 1;                  /* AVC Decoder Configuration Record ver. 1 */
  data[1] = profile_idc;        /* profile_idc                             */
  data[2] = profile_comp;       /* profile_compability                     */
  data[3] = level_idc;          /* level_idc                               */
  data[4] = 0xfc | (nl - 1);    /* nal_length_size_minus1                  */
  data[5] = 0xe0 | num_sps;     /* number of SPSs */

  data += 6;
  GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (sps));
  memcpy (data + 2, GST_BUFFER_DATA (sps), GST_BUFFER_SIZE (sps));
  data += 2 + GST_BUFFER_SIZE (sps);

  data[0] = num_pps;
  data++;
  GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (pps));
  memcpy (data + 2, GST_BUFFER_DATA (pps), GST_BUFFER_SIZE (pps));
  data += 2 + GST_BUFFER_SIZE (pps);

  return buf;
}

static void
_gst_mss_stream_add_h264_codec_data (GstCaps * caps, const gchar * codecdatastr)
{
  GValue sps_value = { 0, };
  GValue pps_value = { 0, };
  GstBuffer *sps;
  GstBuffer *pps;
  GstBuffer *buffer;
  gchar *sps_str;
  gchar *pps_str;
  GstH264NalUnit nalu;
  GstH264SPS sps_struct;
  GstH264ParserResult parseres;

  /* search for the sps start */
  if (g_str_has_prefix (codecdatastr, "00000001")) {
    sps_str = (gchar *) codecdatastr + 8;
  } else {
    return;                     /* invalid mss codec data */
  }

  /* search for the pps start */
  pps_str = g_strstr_len (sps_str, -1, "00000001");
  if (!pps_str) {
    return;                     /* invalid mss codec data */
  }

  g_value_init (&sps_value, GST_TYPE_BUFFER);
  pps_str[0] = '\0';
  gst_value_deserialize (&sps_value, sps_str);
  pps_str[0] = '0';

  g_value_init (&pps_value, GST_TYPE_BUFFER);
  pps_str = pps_str + 8;
  gst_value_deserialize (&pps_value, pps_str);

  sps = gst_value_get_buffer (&sps_value);
  pps = gst_value_get_buffer (&pps_value);

  nalu.ref_idc = (GST_BUFFER_DATA (sps)[0] & 0x60) >> 5;
  nalu.type = GST_H264_NAL_SPS;
  nalu.size = GST_BUFFER_SIZE (sps);
  nalu.data = GST_BUFFER_DATA (sps);
  nalu.offset = 0;
  nalu.sc_offset = 0;
  nalu.valid = TRUE;

  parseres = gst_h264_parse_sps (&nalu, &sps_struct, TRUE);
  if (parseres == GST_H264_PARSER_OK) {
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
        sps_struct.fps_num, sps_struct.fps_den, NULL);
  }

  buffer = _make_h264_codec_data (sps, pps);
  g_value_reset (&sps_value);
  g_value_reset (&pps_value);

  gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, buffer, NULL);
  gst_buffer_unref (buffer);
}

static GstCaps *
_gst_mss_stream_video_caps_from_qualitylevel_xml (xmlNodePtr node)
{
  GstCaps *caps;
  GstStructure *structure;
  gchar *fourcc = (gchar *) xmlGetProp (node, (xmlChar *) "FourCC");
  gchar *max_width = (gchar *) xmlGetProp (node, (xmlChar *) "MaxWidth");
  gchar *max_height = (gchar *) xmlGetProp (node, (xmlChar *) "MaxHeight");
  gchar *codec_data =
      (gchar *) xmlGetProp (node, (xmlChar *) "CodecPrivateData");

  caps = _gst_mss_stream_video_caps_from_fourcc (fourcc);
  if (!caps)
    goto end;

  structure = gst_caps_get_structure (caps, 0);

  if (max_width)
    gst_structure_set (structure, "width", G_TYPE_INT, atoi (max_width), NULL);
  if (max_height)
    gst_structure_set (structure, "height", G_TYPE_INT, atoi (max_height),
        NULL);

  if (codec_data) {
    if (strcmp (fourcc, "H264") == 0) {
      _gst_mss_stream_add_h264_codec_data (caps, codec_data);
    } else {
      GValue *value = g_new0 (GValue, 1);
      g_value_init (value, GST_TYPE_BUFFER);
      gst_value_deserialize (value, (gchar *) codec_data);
      gst_structure_take_value (structure, "codec_data", value);
    }
  }

end:
  g_free (fourcc);
  g_free (max_width);
  g_free (max_height);
  g_free (codec_data);

  return caps;
}

static GstCaps *
_gst_mss_stream_audio_caps_from_qualitylevel_xml (xmlNodePtr node)
{
  GstCaps *caps;
  GstStructure *structure;
  gchar *fourcc = (gchar *) xmlGetProp (node, (xmlChar *) "FourCC");
  gchar *channels = (gchar *) xmlGetProp (node, (xmlChar *) "Channels");
  gchar *rate = (gchar *) xmlGetProp (node, (xmlChar *) "SamplingRate");
  gchar *codec_data =
      (gchar *) xmlGetProp (node, (xmlChar *) "CodecPrivateData");

  caps = _gst_mss_stream_audio_caps_from_fourcc (fourcc);
  if (!caps)
    goto end;

  structure = gst_caps_get_structure (caps, 0);

  if (channels)
    gst_structure_set (structure, "channels", G_TYPE_INT, atoi (channels),
        NULL);
  if (rate)
    gst_structure_set (structure, "rate", G_TYPE_INT, atoi (rate), NULL);

  if (codec_data) {
    GValue *value = g_new0 (GValue, 1);
    g_value_init (value, GST_TYPE_BUFFER);
    gst_value_deserialize (value, (gchar *) codec_data);
    gst_structure_take_value (structure, "codec_data", value);
  }

end:
  g_free (fourcc);
  g_free (channels);
  g_free (rate);
  g_free (codec_data);

  return caps;
}

guint64
gst_mss_stream_get_timescale (GstMssStream * stream)
{
  gchar *timescale;
  guint64 ts = DEFAULT_TIMESCALE;

  timescale =
      (gchar *) xmlGetProp (stream->xmlnode, (xmlChar *) MSS_PROP_TIMESCALE);
  if (!timescale) {
    timescale =
        (gchar *) xmlGetProp (stream->xmlnode->parent,
        (xmlChar *) MSS_PROP_TIMESCALE);
  }

  if (timescale) {
    ts = strtoull (timescale, NULL, 10);
    g_free (timescale);
  }
  return ts;
}

guint64
gst_mss_manifest_get_timescale (GstMssManifest * manifest)
{
  gchar *timescale;
  guint64 ts = DEFAULT_TIMESCALE;

  timescale =
      (gchar *) xmlGetProp (manifest->xmlrootnode,
      (xmlChar *) MSS_PROP_TIMESCALE);
  if (timescale) {
    ts = strtoull (timescale, NULL, 10);
    g_free (timescale);
  }
  return ts;
}

guint64
gst_mss_manifest_get_duration (GstMssManifest * manifest)
{
  gchar *duration;
  guint64 dur = -1;

  duration =
      (gchar *) xmlGetProp (manifest->xmlrootnode,
      (xmlChar *) MSS_PROP_STREAM_DURATION);
  if (duration) {
    dur = strtoull (duration, NULL, 10);
    g_free (duration);
  }
  return dur;
}


/**
 * Gets the duration in nanoseconds
 */
GstClockTime
gst_mss_manifest_get_gst_duration (GstMssManifest * manifest)
{
  guint64 duration = -1;
  guint64 timescale;
  GstClockTime gstdur = GST_CLOCK_TIME_NONE;

  duration = gst_mss_manifest_get_duration (manifest);
  timescale = gst_mss_manifest_get_timescale (manifest);

  if (duration != -1 && timescale != -1)
    gstdur =
        (GstClockTime) gst_util_uint64_scale_round (duration, GST_SECOND,
        timescale);

  return gstdur;
}

GstCaps *
gst_mss_stream_get_caps (GstMssStream * stream)
{
  GstMssStreamType streamtype = gst_mss_stream_get_type (stream);
  xmlNodePtr qualitylevel = stream->current_quality->data;
  GstCaps *caps = NULL;

  if (streamtype == MSS_STREAM_TYPE_VIDEO)
    caps = _gst_mss_stream_video_caps_from_qualitylevel_xml (qualitylevel);
  else if (streamtype == MSS_STREAM_TYPE_AUDIO)
    caps = _gst_mss_stream_audio_caps_from_qualitylevel_xml (qualitylevel);

  return caps;
}

GstFlowReturn
gst_mss_stream_get_fragment_url (GstMssStream * stream, gchar ** url)
{
  gchar *tmp;
  gchar *bitrate_str;
  gchar *start_time_str;
  GstMssStreamFragment *fragment;

  if (stream->current_fragment == NULL) /* stream is over */
    return GST_FLOW_UNEXPECTED;

  fragment = stream->current_fragment->data;

  bitrate_str =
      (gchar *) xmlGetProp (stream->current_quality->data,
      (xmlChar *) MSS_PROP_BITRATE);
  start_time_str = g_strdup_printf ("%" G_GUINT64_FORMAT, fragment->time);

  tmp = g_regex_replace_literal (stream->regex_bitrate, stream->url,
      strlen (stream->url), 0, bitrate_str, 0, NULL);
  *url = g_regex_replace_literal (stream->regex_position, tmp,
      strlen (tmp), 0, start_time_str, 0, NULL);

  g_free (tmp);
  g_free (start_time_str);
  g_free (bitrate_str);
  return GST_FLOW_OK;
}

GstFlowReturn
gst_mss_stream_advance_fragment (GstMssStream * stream)
{
  if (stream->current_fragment == NULL)
    return GST_FLOW_UNEXPECTED;

  stream->current_fragment = g_list_next (stream->current_fragment);
  if (stream->current_fragment == NULL)
    return GST_FLOW_UNEXPECTED;
  return GST_FLOW_OK;
}

const gchar *
gst_mss_stream_type_name (GstMssStreamType streamtype)
{
  switch (streamtype) {
    case MSS_STREAM_TYPE_VIDEO:
      return "video";
    case MSS_STREAM_TYPE_AUDIO:
      return "audio";
    case MSS_STREAM_TYPE_UNKNOWN:
    default:
      return "unknown";
  }
}

/**
 * Seeks all streams to the fragment that contains the set time
 *
 * @time: time in nanoseconds
 */
gboolean
gst_mss_manifest_seek (GstMssManifest * manifest, guint64 time)
{
  gboolean ret = TRUE;
  GSList *iter;

  for (iter = manifest->streams; iter; iter = g_slist_next (iter)) {
    ret = gst_mss_stream_seek (iter->data, time) & ret;
  }

  return ret;
}

/**
 * Seeks this stream to the fragment that contains the sample at time
 *
 * @time: time in nanoseconds
 */
gboolean
gst_mss_stream_seek (GstMssStream * stream, guint64 time)
{
  GList *iter;
  guint64 timescale;

  timescale = gst_mss_stream_get_timescale (stream);
  time = gst_util_uint64_scale_round (time, timescale, GST_SECOND);

  for (iter = stream->fragments; iter; iter = g_list_next (iter)) {
    GList *next = g_list_next (iter);
    if (next) {
      GstMssStreamFragment *fragment = next->data;

      if (fragment->time > time) {
        stream->current_fragment = iter;
        break;
      }
    } else {
      GstMssStreamFragment *fragment = iter->data;
      if (fragment->time + fragment->duration > time) {
        stream->current_fragment = iter;
      } else {
        stream->current_fragment = NULL;        /* EOS */
      }
      break;
    }
  }

  return TRUE;
}
