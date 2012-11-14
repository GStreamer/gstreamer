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

#include "gstmssmanifest.h"

struct _GstMssManifestStream
{
  xmlNodePtr xmlnode;

  gint selectedQualityIndex;
};

struct _GstMssManifest
{
  xmlDocPtr xml;
  xmlNodePtr xmlrootnode;

  GSList *streams;
};

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
      GstMssManifestStream *stream = g_new0 (GstMssManifestStream, 1);

      manifest->streams = g_slist_append (manifest->streams, stream);
      stream->xmlnode = nodeiter;
    }
  }

  return manifest;
}

void
gst_mss_manifest_free (GstMssManifest * manifest)
{
  g_return_if_fail (manifest != NULL);

  g_slist_free_full (manifest->streams, g_free);

  xmlFreeDoc (manifest->xml);
  g_free (manifest);
}

GSList *
gst_mss_manifest_get_streams (GstMssManifest * manifest)
{
  return manifest->streams;
}

GstMssManifestStreamType
gst_mss_manifest_stream_get_type (GstMssManifestStream * stream)
{
  gchar *prop = (gchar *) xmlGetProp (stream->xmlnode, (xmlChar *) "Type");
  GstMssManifestStreamType ret = MSS_STREAM_TYPE_UNKNOWN;

  if (strcmp (prop, "video") == 0) {
    ret = MSS_STREAM_TYPE_VIDEO;
  } else if (strcmp (prop, "audio") == 0) {
    ret = MSS_STREAM_TYPE_AUDIO;
  }
  xmlFree (prop);
  return ret;
}

static GstCaps *
_gst_mss_manifest_stream_video_caps_from_fourcc (gchar * fourcc)
{
  if (!fourcc)
    return NULL;

  if (strcmp (fourcc, "H264") == 0) {
    return gst_caps_new_simple ("video/x-h264", NULL);
  }
  return NULL;
}

static GstCaps *
_gst_mss_manifest_stream_audio_caps_from_fourcc (gchar * fourcc)
{
  if (!fourcc)
    return NULL;

  if (strcmp (fourcc, "AACL") == 0) {
    return gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 4,
        NULL);
  }
  return NULL;
}

static GstCaps *
_gst_mss_manifest_stream_video_caps_from_qualitylevel_xml (xmlNodePtr node)
{
  GstCaps *caps;
  GstStructure *structure;
  gchar *fourcc = (gchar *) xmlGetProp (node, (xmlChar *) "FourCC");
  gchar *max_width = (gchar *) xmlGetProp (node, (xmlChar *) "MaxWidth");
  gchar *max_height = (gchar *) xmlGetProp (node, (xmlChar *) "MaxHeight");
  gchar *codec_data =
      (gchar *) xmlGetProp (node, (xmlChar *) "CodecPrivateData");

  caps = _gst_mss_manifest_stream_video_caps_from_fourcc (fourcc);
  if (!caps)
    goto end;

  structure = gst_caps_get_structure (caps, 0);

  if (max_width)
    gst_structure_set (structure, "width", G_TYPE_INT, atoi (max_width), NULL);
  if (max_height)
    gst_structure_set (structure, "height", G_TYPE_INT, atoi (max_height),
        NULL);

  if (codec_data) {
    GValue *value = g_new0 (GValue, 1);
    g_value_init (value, GST_TYPE_BUFFER);
    gst_value_deserialize (value, (gchar *) codec_data);
    gst_structure_take_value (structure, "codec_data", value);
  }

end:
  g_free (fourcc);
  g_free (max_width);
  g_free (max_height);
  g_free (codec_data);

  return caps;
}

static GstCaps *
_gst_mss_manifest_stream_audio_caps_from_qualitylevel_xml (xmlNodePtr node)
{
  GstCaps *caps;
  GstStructure *structure;
  gchar *fourcc = (gchar *) xmlGetProp (node, (xmlChar *) "FourCC");
  gchar *channels = (gchar *) xmlGetProp (node, (xmlChar *) "Channels");
  gchar *rate = (gchar *) xmlGetProp (node, (xmlChar *) "SamplingRate");
  gchar *codec_data =
      (gchar *) xmlGetProp (node, (xmlChar *) "CodecPrivateData");

  caps = _gst_mss_manifest_stream_audio_caps_from_fourcc (fourcc);
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

GstCaps *
gst_mss_manifest_stream_get_caps (GstMssManifestStream * stream)
{
  GstMssManifestStreamType streamtype =
      gst_mss_manifest_stream_get_type (stream);

  /* TODO properly get the stream */
  xmlNodePtr qualitylevel = stream->xmlnode->children;
  while (strcmp ((gchar *) qualitylevel->name, "QualityLevel")) {
    qualitylevel = qualitylevel->next;
  }

  if (streamtype == MSS_STREAM_TYPE_VIDEO)
    return
        _gst_mss_manifest_stream_video_caps_from_qualitylevel_xml
        (qualitylevel);
  else if (streamtype == MSS_STREAM_TYPE_AUDIO)
    return
        _gst_mss_manifest_stream_audio_caps_from_qualitylevel_xml
        (qualitylevel);

  return NULL;
}

const gchar *
gst_mss_manifest_stream_type_name (GstMssManifestStreamType streamtype)
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
