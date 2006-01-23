/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#ifndef __SDP_MESSAGE_H__
#define __SDP_MESSAGE_H__

#include <glib.h>

#include <rtspdefs.h>

G_BEGIN_DECLS

typedef struct {
  gchar *username;
  gchar *sess_id;
  gchar *sess_version;
  gchar *nettype;
  gchar *addrtype;
  gchar *addr;
} SDPOrigin;

typedef struct {
  gchar *nettype; 
  gchar *addrtype; 
  gchar *address;
  gint   ttl;
  gint   addr_number;
} SDPConnection;

typedef struct {
  gchar *bwtype;
  gint   bandwidth;
} SDPBandwidth;

typedef struct {
  gchar *start;
  gchar *stop;
  gint   n_repeat;
  gchar **repeat;
} SDPTime;

typedef struct {
  gchar *time;
  gchar *typed_time;
} SDPZone;

typedef struct {
  gchar *type;
  gchar *data;
} SDPKey;

typedef struct {
  gchar *key;
  gchar *value;
} SDPAttribute;

typedef struct {
  gchar         *media;
  gint           port;
  gint           num_ports;
  gchar         *proto;
  GArray        *fmts;
  gchar         *information;
  GArray        *connections;
  GArray        *bandwidths;
  SDPKey         key;
  GArray        *attributes;
} SDPMedia;

typedef struct {
  gchar         *version;
  SDPOrigin      origin;
  gchar         *session_name;
  gchar         *information;
  gchar         *uri;   
  GArray        *emails;
  GArray        *phones;
  SDPConnection  connection;
  GArray        *bandwidths;
  GArray        *times;
  GArray        *zones;
  SDPKey         key;
  GArray        *attributes;
  GArray        *medias;
} SDPMessage;

/* Session descriptions */
RTSPResult      sdp_message_new                 (SDPMessage **msg);
RTSPResult      sdp_message_init                (SDPMessage *msg);
RTSPResult      sdp_message_clean               (SDPMessage *msg);
RTSPResult      sdp_message_free                (SDPMessage *msg);

RTSPResult      sdp_message_parse_buffer        (guint8 *data, guint size, SDPMessage *msg);

RTSPResult      sdp_message_set_version         (SDPMessage *msg, gchar *version);
gchar*          sdp_message_get_version         (SDPMessage *msg);
RTSPResult      sdp_message_set_origin          (SDPMessage *msg, gchar *username, gchar *sess_id,
                                                 gchar *sess_version, gchar *nettype,
                                                 gchar *addrtype, gchar *addr);
SDPOrigin*      sdp_message_get_origin          (SDPMessage *msg);
RTSPResult      sdp_message_set_session_name    (SDPMessage *msg, gchar *session_name);
gchar*          sdp_message_get_session_name    (SDPMessage *msg);
RTSPResult      sdp_message_set_information     (SDPMessage *msg, gchar *information);
gchar*          sdp_message_get_information     (SDPMessage *msg);
RTSPResult      sdp_message_set_uri             (SDPMessage *msg, gchar *uri);
gchar*          sdp_message_get_uri             (SDPMessage *msg);
gint            sdp_message_emails_len          (SDPMessage *msg);
gchar*          sdp_message_get_email           (SDPMessage *msg, gint i);
RTSPResult      sdp_message_add_email           (SDPMessage *msg, gchar *email);
gint            sdp_message_phones_len          (SDPMessage *msg);
gchar*          sdp_message_get_phone           (SDPMessage *msg, gint i);
RTSPResult      sdp_message_add_phone           (SDPMessage *msg, gchar *phone);
RTSPResult      sdp_message_set_connection      (SDPMessage *msg, gchar *nettype, gchar *addrtype,
                                                 gchar *address, gint ttl, gint addr_number);
SDPConnection*  sdp_message_get_connection      (SDPMessage *msg);
gint            sdp_message_bandwidths_len      (SDPMessage *msg);
SDPBandwidth*   sdp_message_get_bandwidth       (SDPMessage *msg, gint i);
RTSPResult      sdp_message_add_bandwidth       (SDPMessage *msg, gchar *bwtype, gint bandwidth);
gint            sdp_message_times_len           (SDPMessage *msg);
SDPTime*        sdp_message_get_time            (SDPMessage *msg, gint i);
RTSPResult      sdp_message_add_time            (SDPMessage *msg, gchar *time);
gint            sdp_message_zones_len           (SDPMessage *msg);
SDPZone*        sdp_message_get_zone            (SDPMessage *msg, gint i);
RTSPResult      sdp_message_add_zone            (SDPMessage *msg, gchar *time, gchar *typed_time);
RTSPResult      sdp_message_set_key             (SDPMessage *msg, gchar *type, gchar *data);
SDPKey*         sdp_message_get_key             (SDPMessage *msg);
gint            sdp_message_attributes_len      (SDPMessage *msg);
SDPAttribute*   sdp_message_get_attribute       (SDPMessage *msg, gint i);
gchar*          sdp_message_get_attribute_val   (SDPMessage *msg, gchar *key);
RTSPResult      sdp_message_add_attribute       (SDPMessage *msg, gchar *key, gchar *value);
gint            sdp_message_medias_len          (SDPMessage *msg);
SDPMedia*       sdp_message_get_media           (SDPMessage *msg, gint i);
RTSPResult      sdp_message_add_media           (SDPMessage *msg, SDPMedia *media);


RTSPResult      sdp_message_dump                (SDPMessage *msg);

/* Media descriptions */
RTSPResult      sdp_media_new                   (SDPMedia **media);
RTSPResult      sdp_media_init                  (SDPMedia *media);
RTSPResult      sdp_media_clean                 (SDPMedia *media);
RTSPResult      sdp_media_free                  (SDPMedia *media);

gchar*          sdp_media_get_attribute_val     (SDPMedia *media, gchar *key);
gchar*          sdp_media_get_format            (SDPMedia *media, gint i);


G_END_DECLS

#endif /* __SDP_MESSAGE_H__ */
