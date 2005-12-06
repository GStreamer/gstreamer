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

#include <stdlib.h>
#include <string.h>

#include "sdpmessage.h"

#define FREE_STRING(field)      g_free ((field)); (field) = NULL;
#define FREE_ARRAY(field)       \
G_STMT_START {                  \
  if (field)                    \
    g_array_free (field, TRUE); \
  field = NULL;                 \
} G_STMT_END
#define REPLACE_STRING(field,val)       FREE_STRING(field);field=g_strdup (val);

#define INIT_ARRAY(field,type)  \
G_STMT_START {                  \
  if (field)                    \
    g_array_set_size (field,0); \
  else                          \
    field = g_array_new (FALSE, TRUE, sizeof(type));    \
} G_STMT_END

#define DEFINE_STRING_SETTER(field)                                     \
RTSPResult sdp_message_set_##field (SDPMessage *msg, gchar *val) {      \
  g_free (msg->field);                                                  \
  msg->field = g_strdup (val);                                          \
  return RTSP_OK;                                                       \
}
#define DEFINE_STRING_GETTER(field)                                     \
char* sdp_message_get_##field (SDPMessage *msg) {                       \
  return msg->field;                                                    \
}

#define DEFINE_ARRAY_LEN(field)                                         \
gint sdp_message_##field##_len (SDPMessage *msg) {                      \
  return ((msg)->field->len);                                           \
}
#define DEFINE_ARRAY_GETTER(method,field,type)                                  \
type sdp_message_get_##method (SDPMessage *msg, gint i) {               \
  return g_array_index ((msg)->field, type, i);                         \
}
#define DEFINE_ARRAY_P_GETTER(method,field,type)                                        \
type * sdp_message_get_##method (SDPMessage *msg, gint i) {             \
  return &g_array_index ((msg)->field, type, i);                                \
}
#define DEFINE_ARRAY_ADDER(method,field,type,dup_method)                        \
RTSPResult sdp_message_add_##method (SDPMessage *msg, type val) {       \
  type v = dup_method(val);                                             \
  g_array_append_val((msg)->field, v);                                  \
  return RTSP_OK;                                                       \
}



RTSPResult
sdp_message_new (SDPMessage ** msg)
{
  SDPMessage *newmsg;

  if (msg == NULL)
    return RTSP_EINVAL;

  newmsg = g_new0 (SDPMessage, 1);

  *msg = newmsg;

  return sdp_message_init (newmsg);
}

RTSPResult
sdp_message_init (SDPMessage * msg)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  FREE_STRING (msg->version);
  FREE_STRING (msg->origin.username);
  FREE_STRING (msg->origin.sess_id);
  FREE_STRING (msg->origin.sess_version);
  FREE_STRING (msg->origin.nettype);
  FREE_STRING (msg->origin.addrtype);
  FREE_STRING (msg->origin.addr);
  FREE_STRING (msg->session_name);
  FREE_STRING (msg->information);
  FREE_STRING (msg->uri);
  INIT_ARRAY (msg->emails, gchar *);
  INIT_ARRAY (msg->phones, gchar *);
  FREE_STRING (msg->connection.nettype);
  FREE_STRING (msg->connection.addrtype);
  FREE_STRING (msg->connection.address);
  msg->connection.ttl = 0;
  msg->connection.addr_number = 0;
  INIT_ARRAY (msg->bandwidths, SDPBandwidth);
  INIT_ARRAY (msg->times, SDPTime);
  INIT_ARRAY (msg->zones, SDPZone);
  FREE_STRING (msg->key.type);
  FREE_STRING (msg->key.data);
  INIT_ARRAY (msg->attributes, SDPAttribute);
  INIT_ARRAY (msg->medias, SDPMedia);

  return RTSP_OK;
}

RTSPResult
sdp_message_clean (SDPMessage * msg)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  FREE_ARRAY (msg->emails);
  FREE_ARRAY (msg->phones);
  FREE_ARRAY (msg->bandwidths);
  FREE_ARRAY (msg->times);
  FREE_ARRAY (msg->zones);
  FREE_ARRAY (msg->attributes);
  FREE_ARRAY (msg->medias);

  return RTSP_OK;
}

RTSPResult
sdp_message_free (SDPMessage * msg)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  sdp_message_clean (msg);

  g_free (msg);

  return RTSP_OK;
}


RTSPResult
sdp_media_new (SDPMedia ** media)
{
  SDPMedia *newmedia;

  if (media == NULL)
    return RTSP_EINVAL;

  newmedia = g_new0 (SDPMedia, 1);

  *media = newmedia;

  return sdp_media_init (newmedia);
}

RTSPResult
sdp_media_init (SDPMedia * media)
{
  if (media == NULL)
    return RTSP_EINVAL;

  FREE_STRING (media->media);
  media->port = 0;
  media->num_ports = 0;
  FREE_STRING (media->proto);
  INIT_ARRAY (media->fmts, gchar *);
  FREE_STRING (media->information);
  INIT_ARRAY (media->connections, SDPConnection);
  INIT_ARRAY (media->bandwidths, SDPBandwidth);
  FREE_STRING (media->key.type);
  FREE_STRING (media->key.data);
  INIT_ARRAY (media->attributes, SDPAttribute);

  return RTSP_OK;
}

DEFINE_STRING_SETTER (version);
DEFINE_STRING_GETTER (version);

RTSPResult
sdp_message_set_origin (SDPMessage * msg, gchar * username, gchar * sess_id,
    gchar * sess_version, gchar * nettype, gchar * addrtype, gchar * addr)
{
  REPLACE_STRING (msg->origin.username, username);
  REPLACE_STRING (msg->origin.sess_id, sess_id);
  REPLACE_STRING (msg->origin.sess_version, sess_version);
  REPLACE_STRING (msg->origin.nettype, nettype);
  REPLACE_STRING (msg->origin.addrtype, addrtype);
  REPLACE_STRING (msg->origin.addr, addr);
  return RTSP_OK;
}

SDPOrigin *
sdp_message_get_origin (SDPMessage * msg)
{
  return &msg->origin;
}

DEFINE_STRING_SETTER (session_name);
DEFINE_STRING_GETTER (session_name);
DEFINE_STRING_SETTER (information);
DEFINE_STRING_GETTER (information);
DEFINE_STRING_SETTER (uri);
DEFINE_STRING_GETTER (uri);

DEFINE_ARRAY_LEN (emails);
DEFINE_ARRAY_GETTER (email, emails, gchar *);
DEFINE_ARRAY_ADDER (email, emails, gchar *, g_strdup);

DEFINE_ARRAY_LEN (phones);
DEFINE_ARRAY_GETTER (phone, phones, gchar *);
DEFINE_ARRAY_ADDER (phone, phones, gchar *, g_strdup);

RTSPResult
sdp_message_set_connection (SDPMessage * msg, gchar * nettype, gchar * addrtype,
    gchar * address, gint ttl, gint addr_number)
{
  REPLACE_STRING (msg->connection.nettype, nettype);
  REPLACE_STRING (msg->connection.addrtype, addrtype);
  REPLACE_STRING (msg->connection.address, address);
  msg->connection.ttl = ttl;
  msg->connection.addr_number = addr_number;
  return RTSP_OK;
}

SDPConnection *
sdp_message_get_connection (SDPMessage * msg)
{
  return &msg->connection;
}

DEFINE_ARRAY_LEN (bandwidths);
DEFINE_ARRAY_P_GETTER (bandwidth, bandwidths, SDPBandwidth);

RTSPResult
sdp_message_add_bandwidth (SDPMessage * msg, gchar * bwtype, gint bandwidth)
{
  SDPBandwidth bw;

  bw.bwtype = g_strdup (bwtype);
  bw.bandwidth = bandwidth;

  g_array_append_val (msg->bandwidths, bw);

  return RTSP_OK;
}

DEFINE_ARRAY_LEN (times);
DEFINE_ARRAY_P_GETTER (time, times, SDPTime);

RTSPResult
sdp_message_add_time (SDPMessage * msg, gchar * time)
{
  return RTSP_OK;
}

DEFINE_ARRAY_LEN (zones);
DEFINE_ARRAY_P_GETTER (zone, zones, SDPZone);
RTSPResult
sdp_message_add_zone (SDPMessage * msg, gchar * time, gchar * typed_time)
{
  SDPZone zone;

  zone.time = g_strdup (time);
  zone.typed_time = g_strdup (typed_time);

  g_array_append_val (msg->zones, zone);

  return RTSP_OK;
}

RTSPResult
sdp_message_set_key (SDPMessage * msg, gchar * type, gchar * data)
{
  REPLACE_STRING (msg->key.type, type);
  REPLACE_STRING (msg->key.data, data);
  return RTSP_OK;
}

SDPKey *
sdp_message_get_key (SDPMessage * msg)
{
  return &msg->key;
}


DEFINE_ARRAY_LEN (attributes);
DEFINE_ARRAY_P_GETTER (attribute, attributes, SDPAttribute);
gchar *
sdp_message_get_attribute_val (SDPMessage * msg, gchar * key)
{
  gint i;

  for (i = 0; i < msg->attributes->len; i++) {
    SDPAttribute *attr;

    attr = &g_array_index (msg->attributes, SDPAttribute, i);
    if (!strcmp (attr->key, key))
      return attr->value;
  }
  return NULL;
}

RTSPResult
sdp_message_add_attribute (SDPMessage * msg, gchar * key, gchar * value)
{
  SDPAttribute attr;

  attr.key = g_strdup (key);
  attr.value = g_strdup (value);

  g_array_append_val (msg->attributes, attr);

  return RTSP_OK;
}

DEFINE_ARRAY_LEN (medias);
DEFINE_ARRAY_P_GETTER (media, medias, SDPMedia);
RTSPResult
sdp_message_add_media (SDPMessage * msg, SDPMedia * media)
{
  gint len;
  SDPMedia *nmedia;

  len = msg->medias->len;
  g_array_set_size (msg->medias, len + 1);
  nmedia = &g_array_index (msg->medias, SDPMedia, len);

  memcpy (nmedia, media, sizeof (SDPMedia));

  return RTSP_OK;
}

/* media access */

RTSPResult
sdp_media_add_attribute (SDPMedia * media, gchar * key, gchar * value)
{
  SDPAttribute attr;

  attr.key = g_strdup (key);
  attr.value = g_strdup (value);

  g_array_append_val (media->attributes, attr);

  return RTSP_OK;
}

RTSPResult
sdp_media_add_bandwidth (SDPMedia * media, gchar * bwtype, gint bandwidth)
{
  SDPBandwidth bw;

  bw.bwtype = g_strdup (bwtype);
  bw.bandwidth = bandwidth;

  g_array_append_val (media->bandwidths, bw);

  return RTSP_OK;
}

RTSPResult
sdp_media_add_format (SDPMedia * media, gchar * format)
{
  gchar *fmt;

  fmt = g_strdup (format);

  g_array_append_val (media->fmts, fmt);

  return RTSP_OK;
}

gchar *
sdp_media_get_attribute_val (SDPMedia * media, gchar * key)
{
  gint i;

  for (i = 0; i < media->attributes->len; i++) {
    SDPAttribute *attr;

    attr = &g_array_index (media->attributes, SDPAttribute, i);
    if (!strcmp (attr->key, key))
      return attr->value;
  }
  return NULL;
}

gchar *
sdp_media_get_format (SDPMedia * media, gint i)
{
  if (i >= media->fmts->len)
    return NULL;
  return g_array_index (media->fmts, gchar *, i);
}

static void
read_string (gchar * dest, gint size, gchar ** src)
{
  gint idx;

  idx = 0;
  /* skip spaces */
  while (g_ascii_isspace (**src))
    (*src)++;

  while (!g_ascii_isspace (**src) && **src != '\0') {
    if (idx < size - 1)
      dest[idx++] = **src;
    (*src)++;
  }
  if (size > 0)
    dest[idx] = '\0';
}

static void
read_string_del (gchar * dest, gint size, gchar del, gchar ** src)
{
  gint idx;

  idx = 0;
  /* skip spaces */
  while (g_ascii_isspace (**src))
    (*src)++;

  while (**src != del && **src != '\0') {
    if (idx < size - 1)
      dest[idx++] = **src;
    (*src)++;
  }
  if (size > 0)
    dest[idx] = '\0';
}

enum
{
  SDP_SESSION,
  SDP_MEDIA,
};

typedef struct
{
  gint state;
  SDPMessage *msg;
  SDPMedia *media;
} SDPContext;

static gboolean
sdp_parse_line (SDPContext * c, gchar type, gchar * buffer)
{
  gchar str[4096];
  gchar *p = buffer;

#define READ_STRING(field) read_string (str, sizeof(str), &p);REPLACE_STRING (field, str);
#define READ_INT(field) read_string (str, sizeof(str), &p);field = atoi(str);

  switch (type) {
    case 'v':
      if (buffer[0] != '0')
        g_warning ("wrong SDP version");
      sdp_message_set_version (c->msg, buffer);
      break;
    case 'o':
      READ_STRING (c->msg->origin.username);
      READ_STRING (c->msg->origin.sess_id);
      READ_STRING (c->msg->origin.sess_version);
      READ_STRING (c->msg->origin.nettype);
      READ_STRING (c->msg->origin.addrtype);
      READ_STRING (c->msg->origin.addr);
      break;
    case 's':
      REPLACE_STRING (c->msg->session_name, buffer);
      break;
    case 'i':
      if (c->state == SDP_SESSION) {
        REPLACE_STRING (c->msg->information, buffer);
      } else {
        REPLACE_STRING (c->media->information, buffer);
      }
      break;
    case 'u':
      REPLACE_STRING (c->msg->uri, buffer);
      break;
    case 'e':
      sdp_message_add_email (c->msg, buffer);
      break;
    case 'p':
      sdp_message_add_phone (c->msg, buffer);
      break;
    case 'c':
      READ_STRING (c->msg->connection.nettype);
      READ_STRING (c->msg->connection.addrtype);
      READ_STRING (c->msg->connection.address);
      READ_INT (c->msg->connection.ttl);
      READ_INT (c->msg->connection.addr_number);
      break;
    case 'b':
    {
      gchar str2[4096];

      read_string_del (str, sizeof (str), ':', &p);
      read_string (str2, sizeof (str2), &p);
      if (c->state == SDP_SESSION)
        sdp_message_add_bandwidth (c->msg, str, atoi (str2));
      else
        sdp_media_add_bandwidth (c->media, str, atoi (str2));
      break;
    }
    case 't':
      break;
    case 'k':

      break;
    case 'a':
      read_string_del (str, sizeof (str), ':', &p);
      if (p != '\0')
        p++;
      if (c->state == SDP_SESSION)
        sdp_message_add_attribute (c->msg, str, p);
      else
        sdp_media_add_attribute (c->media, str, p);
      break;
    case 'm':
    {
      gchar *slash;
      SDPMedia nmedia = { 0 };

      c->state = SDP_MEDIA;
      sdp_media_init (&nmedia);

      READ_STRING (nmedia.media);
      read_string (str, sizeof (str), &p);
      slash = g_strrstr (str, "/");
      if (slash) {
        *slash = '\0';
        nmedia.port = atoi (str);
        nmedia.num_ports = atoi (slash + 1);
      } else {
        nmedia.port = atoi (str);
        nmedia.num_ports = -1;
      }
      READ_STRING (nmedia.proto);
      do {
        read_string (str, sizeof (str), &p);
        sdp_media_add_format (&nmedia, str);
      } while (*p != '\0');

      sdp_message_add_media (c->msg, &nmedia);
      c->media =
          &g_array_index (c->msg->medias, SDPMedia, c->msg->medias->len - 1);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

RTSPResult
sdp_message_parse_buffer (guint8 * data, guint size, SDPMessage * msg)
{
  gchar *p;
  SDPContext c;
  gchar type;
  gchar buffer[4096];
  gint idx = 0;

  if (msg == NULL || data == NULL || size == 0)
    return RTSP_EINVAL;

  c.state = SDP_SESSION;
  c.msg = msg;
  c.media = NULL;

  p = (gchar *) data;
  while (TRUE) {
    while (g_ascii_isspace (*p))
      p++;

    type = *p++;
    if (type == '\0')
      break;

    if (*p != '=')
      goto line_done;
    p++;

    idx = 0;
    while (*p != '\n' && *p != '\r' && *p != '\0') {
      if (idx < sizeof (buffer) - 1)
        buffer[idx++] = *p;
      p++;
    }
    buffer[idx] = '\0';
    sdp_parse_line (&c, type, buffer);

  line_done:
    while (*p != '\n' && *p != '\0')
      p++;
    if (*p == '\n')
      p++;
  }

  return RTSP_OK;
}

static void
print_media (SDPMedia * media)
{
  g_print ("   media:       '%s'\n", media->media);
  g_print ("   port:        '%d'\n", media->port);
  g_print ("   num_ports:   '%d'\n", media->num_ports);
  g_print ("   proto:       '%s'\n", media->proto);
  if (media->fmts->len > 0) {
    gint i;

    g_print ("   formats:\n");
    for (i = 0; i < media->fmts->len; i++) {
      g_print ("    format  '%s'\n", g_array_index (media->fmts, gchar *, i));
    }
  }
  g_print ("   information: '%s'\n", media->information);
  g_print ("   key:\n");
  g_print ("    type:       '%s'\n", media->key.type);
  g_print ("    data:       '%s'\n", media->key.data);
  if (media->attributes->len > 0) {
    gint i;

    g_print ("   attributes:\n");
    for (i = 0; i < media->attributes->len; i++) {
      SDPAttribute *attr = &g_array_index (media->attributes, SDPAttribute, i);

      g_print ("    attribute '%s' : '%s'\n", attr->key, attr->value);
    }
  }
}

RTSPResult
sdp_message_dump (SDPMessage * msg)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  g_print ("sdp packet %p:\n", msg);
  g_print (" version:       '%s'\n", msg->version);
  g_print (" origin:\n");
  g_print ("  username:     '%s'\n", msg->origin.username);
  g_print ("  sess_id:      '%s'\n", msg->origin.sess_id);
  g_print ("  sess_version: '%s'\n", msg->origin.sess_version);
  g_print ("  nettype:      '%s'\n", msg->origin.nettype);
  g_print ("  addrtype:     '%s'\n", msg->origin.addrtype);
  g_print ("  addr:         '%s'\n", msg->origin.addr);
  g_print (" session_name:  '%s'\n", msg->session_name);
  g_print (" information:   '%s'\n", msg->information);
  g_print (" uri:           '%s'\n", msg->uri);
  if (msg->emails->len > 0) {
    gint i;

    g_print (" emails:\n");
    for (i = 0; i < msg->emails->len; i++) {
      g_print ("  email '%s'\n", g_array_index (msg->emails, gchar *, i));
    }
  }
  if (msg->phones->len > 0) {
    gint i;

    g_print (" phones:\n");
    for (i = 0; i < msg->phones->len; i++) {
      g_print ("  phone '%s'\n", g_array_index (msg->phones, gchar *, i));
    }
  }
  g_print (" connection:\n");
  g_print ("  nettype:      '%s'\n", msg->connection.nettype);
  g_print ("  addrtype:     '%s'\n", msg->connection.addrtype);
  g_print ("  address:      '%s'\n", msg->connection.address);
  g_print ("  ttl:          '%d'\n", msg->connection.ttl);
  g_print ("  addr_number:  '%d'\n", msg->connection.addr_number);
  g_print (" key:\n");
  g_print ("  type:         '%s'\n", msg->key.type);
  g_print ("  data:         '%s'\n", msg->key.data);
  if (msg->attributes->len > 0) {
    gint i;

    g_print (" attributes:\n");
    for (i = 0; i < msg->attributes->len; i++) {
      SDPAttribute *attr = &g_array_index (msg->attributes, SDPAttribute, i);

      g_print ("  attribute '%s' : '%s'\n", attr->key, attr->value);
    }
  }
  if (msg->medias->len > 0) {
    gint i;

    g_print (" medias:\n");
    for (i = 0; i < msg->medias->len; i++) {
      g_print ("  media %d:\n", i);
      print_media (&g_array_index (msg->medias, SDPMedia, i));
    }
  }


  return RTSP_OK;
}
