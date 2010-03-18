/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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
 * SECTION:element-rtspgoogle
 *
 * A Google RTSP extension
 *
 * Last reviewed on 2008-08-20 (0.10.10)
 */

#include <string.h>

#include <gst/rtsp/gstrtspextension.h>

#include "gstrtspgoogle.h"

GST_DEBUG_CATEGORY_STATIC (rtspgoogle_debug);
#define GST_CAT_DEFAULT (rtspgoogle_debug)

#define SERVER_PREFIX "Google RTSP"

static GstRTSPResult
gst_rtsp_google_before_send (GstRTSPExtension * ext, GstRTSPMessage * request)
{
  GstRTSPGoogle *ctx = (GstRTSPGoogle *) ext;

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
gst_rtsp_google_after_send (GstRTSPExtension * ext, GstRTSPMessage * req,
    GstRTSPMessage * resp)
{
  GstRTSPGoogle *ctx = (GstRTSPGoogle *) ext;

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
gst_rtsp_google_get_transports (GstRTSPExtension * ext,
    GstRTSPLowerTrans protocols, gchar ** transport)
{
  GstRTSPResult res;
  GstRTSPGoogle *ctx = (GstRTSPGoogle *) ext;
  GString *result;

  if (!ctx->active)
    return GST_RTSP_OK;

  /* always only suggest UDP */
  if (protocols & GST_RTSP_LOWER_TRANS_UDP) {
    result = g_string_new ("");

    GST_DEBUG_OBJECT (ext, "adding UDP unicast");

    g_string_append (result, "RTP/AVP");
    g_string_append (result, ";unicast;client_port=%%u1-%%u2");

    *transport = g_string_free (result, FALSE);

    res = GST_RTSP_OK;
  } else {
    res = GST_RTSP_ERROR;
  }

  return res;
}

static void gst_rtsp_google_finalize (GObject * object);

static GstStateChangeReturn gst_rtsp_google_change_state (GstElement * element,
    GstStateChange transition);

static void gst_rtsp_google_extension_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType rtspgoogle_type)
{
  static const GInterfaceInfo rtspextension_info = {
    gst_rtsp_google_extension_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (rtspgoogle_type, GST_TYPE_RTSP_EXTENSION,
      &rtspextension_info);
}

GST_BOILERPLATE_FULL (GstRTSPGoogle, gst_rtsp_google, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_rtsp_google_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class, "Google RTSP Extension",
      "Network/Extension/Protocol",
      "Extends RTSP so that it can handle Google setup",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtsp_google_class_init (GstRTSPGoogleClass * g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTSPGoogleClass *klass;

  klass = (GstRTSPGoogleClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_rtsp_google_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtsp_google_change_state);

  GST_DEBUG_CATEGORY_INIT (rtspgoogle_debug, "rtspgoogle", 0,
      "Google RTSP extension");
}

static void
gst_rtsp_google_init (GstRTSPGoogle * rtspgoogle, GstRTSPGoogleClass * klass)
{
}

static void
gst_rtsp_google_finalize (GObject * object)
{
  GstRTSPGoogle *rtspgoogle;

  rtspgoogle = GST_RTSP_GOOGLE (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_rtsp_google_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTSPGoogle *rtspgoogle;

  rtspgoogle = GST_RTSP_GOOGLE (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_rtsp_google_extension_init (gpointer g_iface, gpointer iface_data)
{
  GstRTSPExtensionInterface *iface = (GstRTSPExtensionInterface *) g_iface;

  iface->before_send = gst_rtsp_google_before_send;
  iface->after_send = gst_rtsp_google_after_send;
  iface->get_transports = gst_rtsp_google_get_transports;
}
