/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTSPSRC_H__
#define __GST_RTSPSRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#include <gst/rtsp/gstrtspconnection.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/rtsp/gstrtspurl.h>
#include <gst/rtsp/gstrtsprange.h>

#include "gstrtspext.h"

#define GST_TYPE_RTSPSRC \
  (gst_rtspsrc_get_type())
#define GST_RTSPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSPSRC,GstRTSPSrc))
#define GST_RTSPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSPSRC,GstRTSPSrcClass))
#define GST_IS_RTSPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSPSRC))
#define GST_IS_RTSPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSPSRC))
#define GST_RTSPSRC_CAST(obj) \
  ((GstRTSPSrc *)(obj))

typedef struct _GstRTSPSrc GstRTSPSrc;
typedef struct _GstRTSPSrcClass GstRTSPSrcClass;

#define GST_RTSP_STATE_GET_LOCK(rtsp)    (GST_RTSPSRC_CAST(rtsp)->state_rec_lock)
#define GST_RTSP_STATE_LOCK(rtsp)        (g_static_rec_mutex_lock (GST_RTSP_STATE_GET_LOCK(rtsp)))
#define GST_RTSP_STATE_UNLOCK(rtsp)      (g_static_rec_mutex_unlock (GST_RTSP_STATE_GET_LOCK(rtsp)))

#define GST_RTSP_STREAM_GET_LOCK(rtsp)   (GST_RTSPSRC_CAST(rtsp)->stream_rec_lock)
#define GST_RTSP_STREAM_LOCK(rtsp)       (g_static_rec_mutex_lock (GST_RTSP_STREAM_GET_LOCK(rtsp)))
#define GST_RTSP_STREAM_UNLOCK(rtsp)     (g_static_rec_mutex_unlock (GST_RTSP_STREAM_GET_LOCK(rtsp)))

typedef struct _GstRTSPConnInfo GstRTSPConnInfo;

struct _GstRTSPConnInfo {
  gchar              *location;
  GstRTSPUrl         *url;
  gchar              *url_str;
  GstRTSPConnection  *connection;
  gboolean            connected;
};

typedef struct _GstRTSPStream GstRTSPStream;

struct _GstRTSPStream {
  gint          id;

  GstRTSPSrc   *parent; /* parent, no extra ref to parent is taken */

  /* pad we expose or NULL when it does not have an actual pad */
  GstPad       *srcpad;
  GstFlowReturn last_ret;
  gboolean      added;
  gboolean      disabled;
  gboolean      eos;
  gboolean      discont;

  /* for interleaved mode */
  guint8        channel[2];
  GstCaps      *caps;
  GstPad       *channelpad[2];

  /* our udp sources */
  GstElement   *udpsrc[2];
  GstPad       *blockedpad;
  gboolean      is_ipv6;

  /* our udp sinks back to the server */
  GstElement   *udpsink[2];
  GstPad       *rtcppad;

  /* fakesrc for sending dummy data */
  GstElement   *fakesrc;

  /* state */
  gint          pt;
  guint         port;
  gboolean      container;
  /* original control url */
  gchar        *control_url;
  guint32       ssrc;
  guint32       seqbase;
  guint64       timebase;

  /* per stream connection */
  GstRTSPConnInfo  conninfo;

  /* session */
  GObject      *session;

  /* bandwidth */
  guint         as_bandwidth;
  guint         rs_bandwidth;
  guint         rr_bandwidth;

  /* destination */
  gchar        *destination;
  gboolean      is_multicast;
  guint         ttl;
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

struct _GstRTSPSrc {
  GstBin           parent;

  /* task and mutex for interleaved mode */
  gboolean         interleaved;
  GstTask         *task;
  GStaticRecMutex *stream_rec_lock;
  GstSegment       segment;
  gboolean         running;
  gboolean         need_range;
  gboolean         skip;
  gint             free_channel;
  GstEvent        *close_segment;
  GstEvent        *start_segment;
  GstClockTime     base_time;

  /* UDP mode loop */
  gint             loop_cmd;
  gboolean         ignore_timeout;
  gboolean         waiting;
  gboolean         open_error;

  /* mutex for protecting state changes */
  GStaticRecMutex *state_rec_lock;

  GstSDPMessage   *sdp;
  gboolean         from_sdp;
  gint             numstreams;
  GList           *streams;
  GstStructure    *props;
  gboolean         need_activate;

  /* properties */
  GstRTSPLowerTrans protocols;
  gboolean          debug;
  guint             retry;
  guint64           udp_timeout;
  GTimeVal          tcp_timeout;
  GTimeVal         *ptcp_timeout;
  guint             latency;
  guint             connection_speed;
  GstRTSPNatMethod  nat_method;
  gboolean          do_rtcp;
  gchar            *proxy_host;
  guint             proxy_port;
  gchar            *proxy_user;
  gchar            *proxy_passwd;
  guint             rtp_blocksize;
  gchar            *user_id;
  gchar            *user_pw;
  gint              buffer_mode;
  GstRTSPRange      client_port_range;
  gint              udp_buffer_size;
  gboolean          short_header;

  /* state */
  GstRTSPState       state;
  gchar             *content_base;
  GstRTSPLowerTrans  cur_protocols;
  gboolean           tried_url_auth;
  gchar             *addr;
  gboolean           need_redirect;
  GstRTSPTimeRange  *range;
  gchar             *control;
  guint              next_port_num;

  /* supported methods */
  gint               methods;

  gboolean           seekable;
  GstClockTime       last_pos;

  /* session management */
  GstElement      *manager;
  gulong           manager_sig_id;
  gulong           manager_ptmap_id;

  GstRTSPConnInfo  conninfo;

  /* a list of RTSP extensions as GstElement */
  GstRTSPExtensionList  *extensions;
};

struct _GstRTSPSrcClass {
  GstBinClass parent_class;
};

GType gst_rtspsrc_get_type(void);

G_END_DECLS

#endif /* __GST_RTSPSRC_H__ */
