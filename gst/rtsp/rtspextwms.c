/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <string.h>

#include "gstrtspsrc.h"
#include "rtspextwms.h"

#define SERVER_PREFIX "WMServer/"
#define HEADER_PREFIX "data:application/vnd.ms.wms-hdr.asfv1;base64,"

typedef struct _RTSPExtWMSCtx RTSPExtWMSCtx;

struct _RTSPExtWMSCtx
{
  RTSPExtensionCtx ctx;

  gboolean active;
};

static RTSPResult
rtsp_ext_wms_before_send (RTSPExtensionCtx * ctx, RTSPMessage * request)
{
  RTSPExtWMSCtx *rext = (RTSPExtWMSCtx *) ctx;

  switch (request->type_data.request.method) {
    case RTSP_OPTIONS:
    {
      /* activate ourselves with the first request */
      rext->active = TRUE;
      break;
    }
    default:
      break;
  }
  return RTSP_OK;
}

static RTSPResult
rtsp_ext_wms_after_send (RTSPExtensionCtx * ctx, RTSPMessage * req,
    RTSPMessage * resp)
{
  RTSPExtWMSCtx *rext = (RTSPExtWMSCtx *) ctx;

  switch (req->type_data.request.method) {
    case RTSP_OPTIONS:
    {
      gchar *server = NULL;

      rtsp_message_get_header (resp, RTSP_HDR_SERVER, &server);
      if (server && g_str_has_prefix (server, SERVER_PREFIX))
        rext->active = TRUE;
      else
        rext->active = FALSE;
      break;
    }
    default:
      break;
  }
  return RTSP_OK;
}


static RTSPResult
rtsp_ext_wms_parse_sdp (RTSPExtensionCtx * ctx, SDPMessage * sdp)
{
  GstRTSPSrc *src = (GstRTSPSrc *) ctx->src;
  RTSPExtWMSCtx *rext = (RTSPExtWMSCtx *) ctx;
  gchar *config, *maxps;
  gint i;

  if (!rext->active)
    return RTSP_OK;

  for (i = 0; (config = sdp_message_get_attribute_val_n (sdp, "pgmpu", i)); i++) {
    if (g_str_has_prefix (config, HEADER_PREFIX)) {
      config += strlen (HEADER_PREFIX);
      gst_structure_set (src->props, "config", G_TYPE_STRING, config, NULL);
      break;
    }
  }
  if (config == NULL)
    goto no_config;

  gst_structure_set (src->props, "config", G_TYPE_STRING, config, NULL);

  maxps = sdp_message_get_attribute_val (sdp, "maxps");
  if (maxps)
    gst_structure_set (src->props, "maxps", G_TYPE_STRING, maxps, NULL);

  gst_structure_set (src->props, "encoding-name", G_TYPE_STRING, "x-asf-pf",
      NULL);
  gst_structure_set (src->props, "media", G_TYPE_STRING, "application", NULL);

  return RTSP_OK;

  /* ERRORS */
no_config:
  {
    GST_DEBUG_OBJECT (src, "Could not find config SDP field, deactivating.");
    rext->active = FALSE;
    return RTSP_OK;
  }
}

static gboolean
rtsp_ext_wms_configure_stream (RTSPExtensionCtx * ctx, GstRTSPStream * stream)
{
  GstRTSPSrc *src = (GstRTSPSrc *) ctx->src;
  GstStructure *s;
  const gchar *encoding;

  s = gst_caps_get_structure (stream->caps, 0);
  encoding = gst_structure_get_string (s, "encoding-name");

  if (!encoding)
    return TRUE;

  GST_DEBUG_OBJECT (src, "%" GST_PTR_FORMAT " encoding-name: %s", stream->caps,
      encoding);

  /* rtx streams do not need to be configured */
  if (!strcmp (encoding, "x-wms-rtx"))
    return FALSE;

  return TRUE;
}

RTSPExtensionCtx *
rtsp_ext_wms_get_context (void)
{
  RTSPExtWMSCtx *res;

  res = g_new0 (RTSPExtWMSCtx, 1);
  res->ctx.parse_sdp = rtsp_ext_wms_parse_sdp;
  res->ctx.before_send = rtsp_ext_wms_before_send;
  res->ctx.after_send = rtsp_ext_wms_after_send;
  res->ctx.configure_stream = rtsp_ext_wms_configure_stream;

  return (RTSPExtensionCtx *) res;
}
