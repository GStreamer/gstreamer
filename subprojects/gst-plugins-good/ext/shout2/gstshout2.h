/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_SHOUT2SEND_H__
#define __GST_SHOUT2SEND_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <shout/shout.h>

G_BEGIN_DECLS

  /* Protocol type enum */
typedef enum {
  SHOUT2SEND_PROTOCOL_XAUDIOCAST = 1,
  SHOUT2SEND_PROTOCOL_ICY,
  SHOUT2SEND_PROTOCOL_HTTP
} GstShout2SendProtocol;


#define GST_TYPE_SHOUT2SEND (gst_shout2send_get_type())
G_DECLARE_FINAL_TYPE (GstShout2send, gst_shout2send, GST, SHOUT2SEND,
    GstBaseSink)

struct _GstShout2send {
  GstBaseSink parent;

  GstShout2SendProtocol protocol;

  GstPoll *timer;

  shout_t *conn;

  guint64 prev_queuelen;
  guint64 data_sent;
  GstClockTime datasent_reset_ts;
  gboolean stalled;
  GstClockTime stalled_ts;

  gchar *ip;
  guint port;
  gchar *password;
  gchar *username;
  gchar *streamname;
  gchar *description;
  gchar *genre;
  gchar *mount;
  gchar *url;
  gboolean connected;
  gboolean ispublic;
  gchar *songmetadata;
  gchar *songartist;
  gchar *songtitle;
  gboolean send_title_info;
  gchar *user_agent;
  gint  format;
  guint timeout;
  guint usage;                   /* SHOUT_USAGE_* */

  GstTagList* tags;
};

GST_ELEMENT_REGISTER_DECLARE (shout2send);

G_END_DECLS

#endif /* __GST_SHOUT2SEND_H__ */
