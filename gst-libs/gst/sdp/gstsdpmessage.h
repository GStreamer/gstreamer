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

#ifndef __GST_SDP_MESSAGE_H__
#define __GST_SDP_MESSAGE_H__

#include <glib.h>

#include <gst/sdp/gstsdp.h>

G_BEGIN_DECLS

typedef struct {
  gchar *username;
  gchar *sess_id;
  gchar *sess_version;
  gchar *nettype;
  gchar *addrtype;
  gchar *addr;
} GstSDPOrigin;

typedef struct {
  gchar *nettype; 
  gchar *addrtype; 
  gchar *address;
  gint   ttl;
  gint   addr_number;
} GstSDPConnection;

#define GST_SDP_BWTYPE_CT 		"CT"  /* conference total */
#define GST_SDP_BWTYPE_AS 		"AS"  /* application specific */
#define GST_SDP_BWTYPE_EXT_PREFIX 	"X-"  /* extension prefix */

typedef struct {
  gchar *bwtype;
  gint   bandwidth;
} GstSDPBandwidth;

typedef struct {
  gchar *start;
  gchar *stop;
  gint   n_repeat;
  gchar **repeat;
} GstSDPTime;

typedef struct {
  gchar *time;
  gchar *typed_time;
} GstSDPZone;

typedef struct {
  gchar *type;
  gchar *data;
} GstSDPKey;

typedef struct {
  gchar *key;
  gchar *value;
} GstSDPAttribute;

typedef struct {
  gchar         *media;
  gint           port;
  gint           num_ports;
  gchar         *proto;
  GArray        *fmts;
  gchar         *information;
  GArray        *connections;
  GArray        *bandwidths;
  GstSDPKey         key;
  GArray        *attributes;
} GstSDPMedia;

typedef struct {
  gchar         *version;
  GstSDPOrigin      origin;
  gchar         *session_name;
  gchar         *information;
  gchar         *uri;   
  GArray        *emails;
  GArray        *phones;
  GstSDPConnection  connection;
  GArray        *bandwidths;
  GArray        *times;
  GArray        *zones;
  GstSDPKey         key;
  GArray        *attributes;
  GArray        *medias;
} GstSDPMessage;

/* Session descriptions */
GstSDPResult      gst_sdp_message_new                 (GstSDPMessage **msg);
GstSDPResult      gst_sdp_message_init                (GstSDPMessage *msg);
GstSDPResult      gst_sdp_message_uninit              (GstSDPMessage *msg);
GstSDPResult      gst_sdp_message_free                (GstSDPMessage *msg);

GstSDPResult      gst_sdp_message_parse_buffer        (guint8 *data, guint size, GstSDPMessage *msg);

GstSDPResult      gst_sdp_message_set_version         (GstSDPMessage *msg, gchar *version);
gchar*            gst_sdp_message_get_version         (GstSDPMessage *msg);
GstSDPResult      gst_sdp_message_set_origin          (GstSDPMessage *msg, gchar *username, gchar *sess_id,
                                                        gchar *sess_version, gchar *nettype,
                                                        gchar *addrtype, gchar *addr);
GstSDPOrigin*     gst_sdp_message_get_origin          (GstSDPMessage *msg);
GstSDPResult      gst_sdp_message_set_session_name    (GstSDPMessage *msg, gchar *session_name);
gchar*            gst_sdp_message_get_session_name    (GstSDPMessage *msg);
GstSDPResult      gst_sdp_message_set_information     (GstSDPMessage *msg, gchar *information);
gchar*            gst_sdp_message_get_information     (GstSDPMessage *msg);
GstSDPResult      gst_sdp_message_set_uri             (GstSDPMessage *msg, gchar *uri);
gchar*            gst_sdp_message_get_uri             (GstSDPMessage *msg);
gint              gst_sdp_message_emails_len          (GstSDPMessage *msg);
gchar*            gst_sdp_message_get_email           (GstSDPMessage *msg, guint idx);
GstSDPResult      gst_sdp_message_add_email           (GstSDPMessage *msg, gchar *email);
gint              gst_sdp_message_phones_len          (GstSDPMessage *msg);
gchar*            gst_sdp_message_get_phone           (GstSDPMessage *msg, guint idx);
GstSDPResult      gst_sdp_message_add_phone           (GstSDPMessage *msg, gchar *phone);
GstSDPResult      gst_sdp_message_set_connection      (GstSDPMessage *msg, gchar *nettype, gchar *addrtype,
                                                       gchar *address, gint ttl, gint addr_number);
GstSDPConnection* gst_sdp_message_get_connection       (GstSDPMessage *msg);
gint              gst_sdp_message_bandwidths_len      (GstSDPMessage *msg);
GstSDPBandwidth*  gst_sdp_message_get_bandwidth       (GstSDPMessage *msg, guint idx);
GstSDPResult      gst_sdp_message_add_bandwidth       (GstSDPMessage *msg, gchar *bwtype, gint bandwidth);
gint              gst_sdp_message_times_len           (GstSDPMessage *msg);
GstSDPTime*       gst_sdp_message_get_time            (GstSDPMessage *msg, guint idx);
GstSDPResult      gst_sdp_message_add_time            (GstSDPMessage *msg, gchar *time);
gint              gst_sdp_message_zones_len           (GstSDPMessage *msg);
GstSDPZone*       gst_sdp_message_get_zone            (GstSDPMessage *msg, guint idx);
GstSDPResult      gst_sdp_message_add_zone            (GstSDPMessage *msg, gchar *time, gchar *typed_time);
GstSDPResult      gst_sdp_message_set_key             (GstSDPMessage *msg, gchar *type, gchar *data);
GstSDPKey*        gst_sdp_message_get_key             (GstSDPMessage *msg);
gint              gst_sdp_message_attributes_len      (GstSDPMessage *msg);
GstSDPAttribute*  gst_sdp_message_get_attribute       (GstSDPMessage *msg, guint idx);
gchar*            gst_sdp_message_get_attribute_val   (GstSDPMessage *msg, gchar *key);
gchar*            gst_sdp_message_get_attribute_val_n (GstSDPMessage *msg, gchar *key, guint nth);
GstSDPResult      gst_sdp_message_add_attribute       (GstSDPMessage *msg, gchar *key, gchar *value);
gint              gst_sdp_message_medias_len          (GstSDPMessage *msg);
GstSDPMedia*      gst_sdp_message_get_media           (GstSDPMessage *msg, guint idx);
GstSDPResult      gst_sdp_message_add_media           (GstSDPMessage *msg, GstSDPMedia *media);


GstSDPResult      gst_sdp_message_dump                (GstSDPMessage *msg);

/* Media descriptions */
GstSDPResult      gst_sdp_media_new                   (GstSDPMedia **media);
GstSDPResult      gst_sdp_media_init                  (GstSDPMedia *media);
GstSDPResult      gst_sdp_media_uninit                (GstSDPMedia *media);
GstSDPResult      gst_sdp_media_free                  (GstSDPMedia *media);

GstSDPResult      gst_sdp_media_add_bandwidth         (GstSDPMedia * media, gchar * bwtype, gint bandwidth);

GstSDPResult      gst_sdp_media_add_attribute         (GstSDPMedia *media, gchar * key, gchar * value);
GstSDPAttribute * gst_sdp_media_get_attribute         (GstSDPMedia *media, guint idx);
gchar*            gst_sdp_media_get_attribute_val     (GstSDPMedia *media, gchar *key);
gchar*            gst_sdp_media_get_attribute_val_n   (GstSDPMedia *media, gchar *key, guint nth);

GstSDPResult      gst_sdp_media_add_format            (GstSDPMedia * media, gchar * format);
gchar*            gst_sdp_media_get_format            (GstSDPMedia *media, guint idx);

G_END_DECLS

#endif /* __GST_SDP_MESSAGE_H__ */
