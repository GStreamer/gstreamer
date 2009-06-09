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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <string.h>

#include "rtsp-params.h"

GstRTSPResult
gst_rtsp_params_set (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request,
    GstRTSPMessage * response)
{
  GstRTSPStatusCode code;

  /* FIXME, actually parse the request based on the mime type and try to repond
   * with a list of the parameters */
  code = GST_RTSP_STS_PARAMETER_NOT_UNDERSTOOD;

  gst_rtsp_message_init_response (response, code,
          gst_rtsp_status_as_text (code), request);

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_params_get (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request,
    GstRTSPMessage * response)
{
  GstRTSPStatusCode code;

  /* FIXME, actually parse the request based on the mime type and try to repond
   * with a list of the parameters */
  code = GST_RTSP_STS_PARAMETER_NOT_UNDERSTOOD;

  gst_rtsp_message_init_response (response, code,
          gst_rtsp_status_as_text (code), request);

  return GST_RTSP_OK;
}
