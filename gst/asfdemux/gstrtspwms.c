/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-rtspwms
 *
 * A WMS RTSP extension
 *
 * Last reviewed on 2007-07-25 (0.10.14)
 */

#include <string.h>

#include <gst/rtsp/gstrtspextension.h>

#include "gstrtspwms.h"

GST_DEBUG_CATEGORY_STATIC (rtspwms_debug);
#define GST_CAT_DEFAULT (rtspwms_debug)

#define SERVER_PREFIX "WMServer/"
#define HEADER_PREFIX "data:application/vnd.ms.wms-hdr.asfv1;base64,"
#define EXTENSION_CMD "application/x-wms-extension-cmd"

static GstRTSPResult
gst_rtsp_wms_before_send (GstRTSPExtension * ext, GstRTSPMessage * request)
{
  GstRTSPWMS *ctx = (GstRTSPWMS *) ext;

  GST_DEBUG_OBJECT (ext, "before send");

  switch (request->type_data.request.method) {
    case GST_RTSP_OPTIONS:
    {
      /* activate ourselves with the first request */
      ctx->active = TRUE;
      break;
    }
    default:
      break;
  }
  return GST_RTSP_OK;
}

static GstRTSPResult
gst_rtsp_wms_after_send (GstRTSPExtension * ext, GstRTSPMessage * req,
    GstRTSPMessage * resp)
{
  GstRTSPWMS *ctx = (GstRTSPWMS *) ext;

  GST_DEBUG_OBJECT (ext, "after send");

  switch (req->type_data.request.method) {
    case GST_RTSP_OPTIONS:
    {
      gchar *server = NULL;

      gst_rtsp_message_get_header (resp, GST_RTSP_HDR_SERVER, &server, 0);
      if (server && g_str_has_prefix (server, SERVER_PREFIX))
        ctx->active = TRUE;
      else
        ctx->active = FALSE;
      break;
    }
    default:
      break;
  }
  return GST_RTSP_OK;
}


static GstRTSPResult
gst_rtsp_wms_parse_sdp (GstRTSPExtension * ext, GstSDPMessage * sdp,
    GstStructure * props)
{
  const gchar *config, *maxps;
  gint i;
  GstRTSPWMS *ctx = (GstRTSPWMS *) ext;

  if (!ctx->active)
    return GST_RTSP_OK;

  for (i = 0; (config = gst_sdp_message_get_attribute_val_n (sdp, "pgmpu", i));
      i++) {
    if (g_str_has_prefix (config, HEADER_PREFIX)) {
      config += strlen (HEADER_PREFIX);
      gst_structure_set (props, "config", G_TYPE_STRING, config, NULL);
      break;
    }
  }
  if (config == NULL)
    goto no_config;

  gst_structure_set (props, "config", G_TYPE_STRING, config, NULL);

  maxps = gst_sdp_message_get_attribute_val (sdp, "maxps");
  if (maxps)
    gst_structure_set (props, "maxps", G_TYPE_STRING, maxps, NULL);

  gst_structure_set (props, "encoding-name", G_TYPE_STRING, "X-ASF-PF", NULL);
  gst_structure_set (props, "media", G_TYPE_STRING, "application", NULL);

  return GST_RTSP_OK;

  /* ERRORS */
no_config:
  {
    GST_DEBUG_OBJECT (ctx, "Could not find config SDP field, deactivating.");
    ctx->active = FALSE;
    return GST_RTSP_OK;
  }
}

static gboolean
gst_rtsp_wms_configure_stream (GstRTSPExtension * ext, GstCaps * caps)
{
  GstRTSPWMS *ctx;
  GstStructure *s;
  const gchar *encoding;

  ctx = (GstRTSPWMS *) ext;
  s = gst_caps_get_structure (caps, 0);
  encoding = gst_structure_get_string (s, "encoding-name");

  if (!encoding)
    return TRUE;

  GST_DEBUG_OBJECT (ctx, "%" GST_PTR_FORMAT " encoding-name: %s", caps,
      encoding);

  /* rtx streams do not need to be configured */
  if (!strcmp (encoding, "X-WMS-RTX"))
    return FALSE;

  return TRUE;
}

static GstRTSPResult
gst_rtsp_wms_receive_request (GstRTSPExtension * ext, GstRTSPMessage * request)
{
  GstRTSPWMS *ctx;
  GstRTSPResult res = GST_RTSP_ENOTIMPL;
  GstRTSPMessage response = { 0 };

  ctx = (GstRTSPWMS *) ext;

  GST_DEBUG_OBJECT (ext, "before send");

  switch (request->type_data.request.method) {
    case GST_RTSP_SET_PARAMETER:
    {
      gchar *content_type = NULL;

      gst_rtsp_message_get_header (request, GST_RTSP_HDR_CONTENT_TYPE,
          &content_type, 0);

      if (content_type && !g_ascii_strcasecmp (content_type, EXTENSION_CMD)) {
        /* parse the command */

        /* default implementation, send OK */
        res = gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK, "OK",
            request);
        if (res < 0)
          goto send_error;

        GST_DEBUG_OBJECT (ctx, "replying with OK");

        /* send reply */
        if ((res = gst_rtsp_extension_send (ext, request, &response)) < 0)
          goto send_error;

        res = GST_RTSP_EEOF;
      }
      break;
    }
    default:
      break;
  }
  return res;

send_error:
  {
    return res;
  }
}

static void gst_rtsp_wms_extension_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GstRTSPWMS, gst_rtsp_wms, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_RTSP_EXTENSION,
        gst_rtsp_wms_extension_init));

static void
gst_rtsp_wms_class_init (GstRTSPWMSClass * g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (rtspwms_debug, "rtspwms", 0, "WMS RTSP extension");

  gst_element_class_set_static_metadata (element_class, "WMS RTSP Extension",
      "Network/Extension/Protocol",
      "Extends RTSP so that it can handle WMS setup",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtsp_wms_init (GstRTSPWMS * rtspwms)
{
}

static void
gst_rtsp_wms_extension_init (gpointer g_iface, gpointer iface_data)
{
  GstRTSPExtensionInterface *iface = (GstRTSPExtensionInterface *) g_iface;

  iface->parse_sdp = gst_rtsp_wms_parse_sdp;
  iface->before_send = gst_rtsp_wms_before_send;
  iface->after_send = gst_rtsp_wms_after_send;
  iface->configure_stream = gst_rtsp_wms_configure_stream;
  iface->receive_request = gst_rtsp_wms_receive_request;
}
