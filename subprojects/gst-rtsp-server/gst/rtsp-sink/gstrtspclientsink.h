/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Wim Taymans <wim@fluendo.com>
 *               <2015> Jan Schmidt <jan at centricular dot com>
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

#ifndef __GST_RTSP_CLIENT_SINK_H__
#define __GST_RTSP_CLIENT_SINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#include <gst/rtsp-server/rtsp-stream.h>
#include <gst/rtsp/rtsp.h>
#include <gio/gio.h>

#define GST_TYPE_RTSP_CLIENT_SINK \
  (gst_rtsp_client_sink_get_type())
#define GST_RTSP_CLIENT_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSP_CLIENT_SINK,GstRTSPClientSink))
#define GST_RTSP_CLIENT_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSP_CLIENT_SINK,GstRTSPClientSinkClass))
#define GST_IS_RTSP_CLIENT_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSP_CLIENT_SINK))
#define GST_IS_RTSP_CLIENT_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSP_CLIENT_SINK))
#define GST_RTSP_CLIENT_SINK_CAST(obj) \
  ((GstRTSPClientSink *)(obj))

typedef struct _GstRTSPClientSink GstRTSPClientSink;
typedef struct _GstRTSPClientSinkClass GstRTSPClientSinkClass;

#define GST_RTSP_STATE_GET_LOCK(rtsp)    (&GST_RTSP_CLIENT_SINK_CAST(rtsp)->state_rec_lock)
#define GST_RTSP_STATE_LOCK(rtsp)        (g_rec_mutex_lock (GST_RTSP_STATE_GET_LOCK(rtsp)))
#define GST_RTSP_STATE_UNLOCK(rtsp)      (g_rec_mutex_unlock (GST_RTSP_STATE_GET_LOCK(rtsp)))

#define GST_RTSP_STREAM_GET_LOCK(rtsp)   (&GST_RTSP_CLIENT_SINK_CAST(rtsp)->stream_rec_lock)
#define GST_RTSP_STREAM_LOCK(rtsp)       (g_rec_mutex_lock (GST_RTSP_STREAM_GET_LOCK(rtsp)))
#define GST_RTSP_STREAM_UNLOCK(rtsp)     (g_rec_mutex_unlock (GST_RTSP_STREAM_GET_LOCK(rtsp)))

typedef struct _GstRTSPConnInfo GstRTSPConnInfo;

struct _GstRTSPConnInfo {
  gchar              *location;
  GstRTSPUrl         *url;
  gchar              *url_str;
  GstRTSPConnection  *connection;
  gboolean            connected;
  gboolean            flushing;

  GMutex              send_lock;
  GMutex              recv_lock;
};

typedef struct _GstRTSPStreamInfo GstRTSPStreamInfo;
typedef struct _GstRTSPStreamContext GstRTSPStreamContext;

struct _GstRTSPStreamContext {
  GstRTSPClientSink *parent;

  guint index;
  /* Index of the SDPMedia in the stored SDP */
  guint sdp_index;

  GstElement *payloader;
  guint payloader_block_id;
  gboolean prerolled;

  /* Stream management object */
  GstRTSPStream *stream;
  gboolean joined;

  /* Secure profile key mgmt */
  GstCaps      *srtcpparams;

  /* per stream connection */
  GstRTSPConnInfo  conninfo;
  /* For interleaved mode */
  guint8        channel[2];

  GstRTSPStreamTransport *stream_transport;

  guint ulpfec_percentage;
};

/**
 * GstRTSPNatMethod:
 * @GST_RTSP_NAT_NONE: none
 * @GST_RTSP_NAT_DUMMY: send dummy packets
 *
 * Different methods for trying to traverse firewalls.
 */
typedef enum
{
  GST_RTSP_NAT_NONE,
  GST_RTSP_NAT_DUMMY
} GstRTSPNatMethod;

struct _GstRTSPClientSink {
  GstBin           parent;

  /* task and mutex for interleaved mode */
  gboolean         interleaved;
  GstTask         *task;
  GRecMutex        stream_rec_lock;
  GstSegment       segment;
  gint             free_channel;

  /* UDP mode loop */
  gint             pending_cmd;
  gint             busy_cmd;
  gboolean         ignore_timeout;
  gboolean         open_error;

  /* mutex for protecting state changes */
  GRecMutex        state_rec_lock;

  GstSDPMessage    *uri_sdp;
  gboolean         from_sdp;

  /* properties */
  GstRTSPLowerTrans protocols;
  gboolean          debug;
  guint             retry;
  guint64           udp_timeout;
  gint64            tcp_timeout;
  guint             latency;
  gboolean          do_rtsp_keep_alive;
  gchar            *proxy_host;
  guint             proxy_port;
  gchar            *proxy_user;        /* from url or property */
  gchar            *proxy_passwd;      /* from url or property */
  gchar            *prop_proxy_id;     /* set via property */
  gchar            *prop_proxy_pw;     /* set via property */
  guint             rtp_blocksize;
  gchar            *user_id;
  gchar            *user_pw;
  GstRTSPRange      client_port_range;
  gint              udp_buffer_size;
  gboolean          udp_reconnect;
  gchar            *multi_iface;
  gboolean          ntp_sync;
  gboolean          use_pipeline_clock;
  GstStructure     *sdes;
  GTlsCertificateFlags tls_validation_flags;
  GTlsDatabase     *tls_database;
  GTlsInteraction  *tls_interaction;
  gint              ntp_time_source;
  gchar            *user_agent;
  GstRTSPPublishClockMode publish_clock_mode;

  /* state */
  GstRTSPState        state;
  gchar              *content_base;
  GstRTSPLowerTrans   cur_protocols;
  gboolean            tried_url_auth;
  gchar              *addr;
  GstRTSPAddressPool *pool;
  gboolean            need_redirect;
  GstRTSPTimeRange   *range;
  gchar              *control;
  guint               next_port_num;
  GstClock           *provided_clock;

  /* supported methods */
  gint               methods;

  /* session management */
  GstRTSPConnInfo  conninfo;

  /* Everything goes in an internal
   * locked-state bin */
  GstBin          *internal_bin;
  /* Set to true when internal bin state
   * >= PAUSED */
  gboolean        prerolled;

  /* TRUE if we posted async-start */
  gboolean         in_async;

  /* TRUE when stream info has been collected */
  gboolean         streams_collected;

  /* TRUE when streams have been blocked */
  guint            n_streams_blocked;
  GMutex           block_streams_lock;
  GCond            block_streams_cond;

  guint            next_pad_id;
  gint             next_dyn_pt;

  GstElement      *rtpbin;

  GList           *contexts;
  GstSDPMessage   cursdp;

  GMutex          send_lock;

  GMutex          preroll_lock;
  GCond           preroll_cond;

  /* TRUE if connection to server has been scheduled */
  gboolean        open_conn_start;
  GMutex          open_conn_lock;
  GCond           open_conn_cond;

  GstClockTime    rtx_time;

  GstRTSPProfile profiles;
  gchar          *server_ip;
};

struct _GstRTSPClientSinkClass {
  GstBinClass parent_class;
};

GType gst_rtsp_client_sink_get_type(void);

G_END_DECLS

#endif /* __GST_RTSP_CLIENT_SINK_H__ */
