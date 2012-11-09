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
