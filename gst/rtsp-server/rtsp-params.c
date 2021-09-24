/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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
/**
 * SECTION:rtsp-params
 * @short_description: Param get and set implementation
 * @see_also: #GstRTSPClient
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-params.h"

/**
 * gst_rtsp_params_set:
 * @client: a #GstRTSPClient
 * @ctx: (transfer none): a #GstRTSPContext
 *
 * Set parameters (not implemented yet)
 *
 * Returns: a #GstRTSPResult
 */
GstRTSPResult
gst_rtsp_params_set (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPStatusCode code;

  /* FIXME, actually parse the request based on the mime type and try to repond
   * with a list of the parameters */
  code = GST_RTSP_STS_PARAMETER_NOT_UNDERSTOOD;

  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_params_get:
 * @client: a #GstRTSPClient
 * @ctx: (transfer none): a #GstRTSPContext
 *
 * Get parameters (not implemented yet)
 *
 * Returns: a #GstRTSPResult
 */
GstRTSPResult
gst_rtsp_params_get (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPStatusCode code;

  /* FIXME, actually parse the request based on the mime type and try to repond
   * with a list of the parameters */
  code = GST_RTSP_STS_PARAMETER_NOT_UNDERSTOOD;

  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  return GST_RTSP_OK;
}
