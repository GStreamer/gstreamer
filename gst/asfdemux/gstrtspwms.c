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

/* elementfactory information */
static const GstElementDetails rtspwms_details =
GST_ELEMENT_DETAILS ("WMS RTSP Extension",
    "Network/Extension/Protocol",
    "Extends RTSP so that it can handle WMS setup",
    "Wim Taymans <wim.taymans@gmail.com>");

#define SERVER_PREFIX "WMServer/"
#define HEADER_PREFIX "data:application/vnd.ms.wms-hdr.asfv1;base64,"

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

static void gst_rtsp_wms_finalize (GObject * object);

static GstStateChangeReturn gst_rtsp_wms_change_state (GstElement * element,
    GstStateChange transition);

static void gst_rtsp_wms_extension_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType rtspwms_type)
{
  static const GInterfaceInfo rtspextension_info = {
    gst_rtsp_wms_extension_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (rtspwms_type, GST_TYPE_RTSP_EXTENSION,
      &rtspextension_info);
}

GST_BOILERPLATE_FULL (GstRTSPWMS, gst_rtsp_wms, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_rtsp_wms_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &rtspwms_details);
}

static void
gst_rtsp_wms_class_init (GstRTSPWMSClass * g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTSPWMSClass *klass;

  klass = (GstRTSPWMSClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_rtsp_wms_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtsp_wms_change_state);

  GST_DEBUG_CATEGORY_INIT (rtspwms_debug, "rtspwms", 0, "WMS RTSP extension");
}

static void
gst_rtsp_wms_init (GstRTSPWMS * rtspwms, GstRTSPWMSClass * klass)
{
}

static void
gst_rtsp_wms_finalize (GObject * object)
{
  GstRTSPWMS *rtspwms;

  rtspwms = GST_RTSP_WMS (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_rtsp_wms_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTSPWMS *rtspwms;

  rtspwms = GST_RTSP_WMS (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_rtsp_wms_extension_init (gpointer g_iface, gpointer iface_data)
{
  GstRTSPExtensionInterface *iface = (GstRTSPExtensionInterface *) g_iface;

  iface->parse_sdp = gst_rtsp_wms_parse_sdp;
  iface->before_send = gst_rtsp_wms_before_send;
  iface->after_send = gst_rtsp_wms_after_send;
  iface->configure_stream = gst_rtsp_wms_configure_stream;
}
