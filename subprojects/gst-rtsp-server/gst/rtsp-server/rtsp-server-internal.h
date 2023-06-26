/* GStreamer
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
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

#ifndef __GST_RTSP_SERVER_INTERNAL_H__
#define __GST_RTSP_SERVER_INTERNAL_H__

#include <glib.h>

G_BEGIN_DECLS

#include "rtsp-stream-transport.h"

/* Internal GstRTSPStreamTransport interface */

typedef gboolean (*GstRTSPBackPressureFunc) (guint8 channel, gpointer user_data);

gboolean                 gst_rtsp_stream_transport_backlog_push  (GstRTSPStreamTransport *trans,
                                                                  GstBuffer *buffer,
                                                                  GstBufferList *buffer_list,
                                                                  gboolean is_rtp);

gboolean                 gst_rtsp_stream_transport_backlog_pop   (GstRTSPStreamTransport *trans,
                                                                  GstBuffer **buffer,
                                                                  GstBufferList **buffer_list,
                                                                  gboolean *is_rtp);

gboolean                 gst_rtsp_stream_transport_backlog_peek_is_rtp (GstRTSPStreamTransport * trans);

gboolean                 gst_rtsp_stream_transport_backlog_is_empty (GstRTSPStreamTransport *trans);

void                     gst_rtsp_stream_transport_clear_backlog (GstRTSPStreamTransport * trans);

void                     gst_rtsp_stream_transport_lock_backlog  (GstRTSPStreamTransport * trans);

void                     gst_rtsp_stream_transport_unlock_backlog (GstRTSPStreamTransport * trans);

void                     gst_rtsp_stream_transport_set_back_pressure_callback (GstRTSPStreamTransport *trans,
                                                                  GstRTSPBackPressureFunc back_pressure_func,
                                                                  gpointer user_data,
                                                                  GDestroyNotify  notify);

gboolean                 gst_rtsp_stream_transport_check_back_pressure (GstRTSPStreamTransport *trans,
                                                                  gboolean is_rtp);

gboolean                 gst_rtsp_stream_is_tcp_receiver (GstRTSPStream * stream);

void                     gst_rtsp_media_set_enable_rtcp (GstRTSPMedia *media, gboolean enable);
void                     gst_rtsp_stream_set_enable_rtcp (GstRTSPStream *stream, gboolean enable);

void                     gst_rtsp_stream_set_drop_delta_units (GstRTSPStream * stream, gboolean drop);

gboolean                 gst_rtsp_stream_install_drop_probe (GstRTSPStream * stream);

G_END_DECLS

#endif /* __GST_RTSP_SERVER_INTERNAL_H__ */
