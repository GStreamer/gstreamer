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

#include <stdlib.h>
#include <string.h>

#include "sdpmessage.h"

/* FIXME, is currently allocated on the stack */
#define MAX_LINE_LEN	1024 * 16

#define FREE_STRING(field)      g_free ((field)); (field) = NULL;
#define FREE_ARRAY(field)       \
G_STMT_START {                  \
  if (field)                    \
    g_array_free (field, TRUE); \
  field = NULL;                 \
} G_STMT_END
#define REPLACE_STRING(field,val)       FREE_STRING(field);field=g_strdup (val);

#define INIT_ARRAY(field,type,init_func)  		\
G_STMT_START {                   			\
  if (field) {                   			\
    gint i;			 			\
    for(i=0; i<field->len; i++)				\
      init_func (&g_array_index(field, type, i));	\
    g_array_set_size (field,0); 			\
  }				 			\
  else                          			\
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
#define DEFINE_ARRAY_GETTER(method,field,type)                          \
type sdp_message_get_##method (SDPMessage *msg, guint idx) {            \
  return g_array_index ((msg)->field, type, idx);                       \
}
#define DEFINE_ARRAY_P_GETTER(method,field,type)                        \
type * sdp_message_get_##method (SDPMessage *msg, guint idx) {          \
  return &g_array_index ((msg)->field, type, idx);                      \
}
#define DEFINE_ARRAY_ADDER(method,field,type,dup_method)                \
RTSPResult sdp_message_add_##method (SDPMessage *msg, type val) {       \
  type v = dup_method(val);                                             \
  g_array_append_val((msg)->field, v);                                  \
  return RTSP_OK;                                                       \
}

static void
sdp_origin_init (SDPOrigin * origin)
{
  FREE_STRING (origin->username);
  FREE_STRING (origin->sess_id);
  FREE_STRING (origin->sess_version);
  FREE_STRING (origin->nettype);
  FREE_STRING (origin->addrtype);
  FREE_STRING (origin->addr);
}

static void
sdp_connection_init (SDPConnection * connection)
{
  FREE_STRING (connection->nettype);
  FREE_STRING (connection->addrtype);
  FREE_STRING (connection->address);
  connection->ttl = 0;
  connection->addr_number = 0;
}

static void
sdp_bandwidth_init (SDPBandwidth * bandwidth)
{
  FREE_STRING (bandwidth->bwtype);
  bandwidth->bandwidth = 0;
}

static void
sdp_time_init (SDPTime * time)
{
  FREE_STRING (time->start);
  FREE_STRING (time->stop);
  time->n_repeat = 0;
}

static void
sdp_zone_init (SDPZone * zone)
{
  FREE_STRING (zone->time);
  FREE_STRING (zone->typed_time);
}

static void
sdp_key_init (SDPKey * key)
{
  FREE_STRING (key->type);
  FREE_STRING (key->data);
}

static void
sdp_attribute_init (SDPAttribute * attr)
{
  FREE_STRING (attr->key);
  FREE_STRING (attr->value);
}

/**
 * sdp_message_new:
 * @msg: pointer to new #SDPMessage
 *
 * Allocate a new SDPMessage and store the result in @msg.
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_message_new (SDPMessage ** msg)
{
  SDPMessage *newmsg;

  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);

  newmsg = g_new0 (SDPMessage, 1);

  *msg = newmsg;

  return sdp_message_init (newmsg);
}

/**
 * sdp_message_init:
 * @msg: an #SDPMessage
 *
 * Initialize @msg so that its contents are as if it was freshly allocated
 * with sdp_message_new(). This function is mostly used to initialize a message
 * allocated on the stack. sdp_message_uninit() undoes this operation.
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_message_init (SDPMessage * msg)
{
  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);

  FREE_STRING (msg->version);
  sdp_origin_init (&msg->origin);
  FREE_STRING (msg->session_name);
  FREE_STRING (msg->information);
  FREE_STRING (msg->uri);
  INIT_ARRAY (msg->emails, gchar *, g_free);
  INIT_ARRAY (msg->phones, gchar *, g_free);
  sdp_connection_init (&msg->connection);
  INIT_ARRAY (msg->bandwidths, SDPBandwidth, sdp_bandwidth_init);
  INIT_ARRAY (msg->times, SDPTime, sdp_time_init);
  INIT_ARRAY (msg->zones, SDPZone, sdp_zone_init);
  sdp_key_init (&msg->key);
  INIT_ARRAY (msg->attributes, SDPAttribute, sdp_attribute_init);
  INIT_ARRAY (msg->medias, SDPMedia, sdp_media_uninit);

  return RTSP_OK;
}

/**
 * sdp_message_uninit:
 * @msg: an #SDPMessage
 *
 * Free all resources allocated in @msg. @msg should not be used anymore after
 * this function. This function should be used when @msg was allocated on the
 * stack and initialized with sdp_message_init().
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_message_uninit (SDPMessage * msg)
{
  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);

  sdp_message_init (msg);

  FREE_ARRAY (msg->emails);
  FREE_ARRAY (msg->phones);
  FREE_ARRAY (msg->bandwidths);
  FREE_ARRAY (msg->times);
  FREE_ARRAY (msg->zones);
  FREE_ARRAY (msg->attributes);
  FREE_ARRAY (msg->medias);

  return RTSP_OK;
}

/**
 * sdp_message_free:
 * @msg: an #SDPMessage
 *
 * Free all resources allocated by @msg. @msg should not be used anymore after
 * this function. This function should be used when @msg was dynamically
 * allocated with sdp_message_new().
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_message_free (SDPMessage * msg)
{
  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);

  sdp_message_uninit (msg);
  g_free (msg);

  return RTSP_OK;
}

/**
 * sdp_media_new:
 * @media: pointer to new #SDPMedia
 *
 * Allocate a new SDPMedia and store the result in @media.
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_media_new (SDPMedia ** media)
{
  SDPMedia *newmedia;

  g_return_val_if_fail (media != NULL, RTSP_EINVAL);

  newmedia = g_new0 (SDPMedia, 1);

  *media = newmedia;

  return sdp_media_init (newmedia);
}

/**
 * sdp_media_init:
 * @media: a #SDPMedia
 *
 * Initialize @media so that its contents are as if it was freshly allocated
 * with sdp_media_new(). This function is mostly used to initialize a media
 * allocated on the stack. sdp_media_uninit() undoes this operation.
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_media_init (SDPMedia * media)
{
  g_return_val_if_fail (media != NULL, RTSP_EINVAL);

  FREE_STRING (media->media);
  media->port = 0;
  media->num_ports = 0;
  FREE_STRING (media->proto);
  INIT_ARRAY (media->fmts, gchar *, g_free);
  FREE_STRING (media->information);
  INIT_ARRAY (media->connections, SDPConnection, sdp_connection_init);
  INIT_ARRAY (media->bandwidths, SDPBandwidth, sdp_bandwidth_init);
  sdp_key_init (&media->key);
  INIT_ARRAY (media->attributes, SDPAttribute, sdp_attribute_init);

  return RTSP_OK;
}

/**
 * sdp_media_uninit:
 * @media: an #SDPMedia
 *
 * Free all resources allocated in @media. @media should not be used anymore after
 * this function. This function should be used when @media was allocated on the
 * stack and initialized with sdp_media_init().
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_media_uninit (SDPMedia * media)
{
  g_return_val_if_fail (media != NULL, RTSP_EINVAL);

  sdp_media_init (media);
  FREE_ARRAY (media->fmts);
  FREE_ARRAY (media->connections);
  FREE_ARRAY (media->bandwidths);
  FREE_ARRAY (media->attributes);

  return RTSP_OK;
}

/**
 * sdp_media_free:
 * @media: an #SDPMedia
 *
 * Free all resources allocated by @media. @media should not be used anymore after
 * this function. This function should be used when @media was dynamically
 * allocated with sdp_media_new().
 *
 * Returns: a #RTSPResult.
 */
RTSPResult
sdp_media_free (SDPMedia * media)
{
  g_return_val_if_fail (media != NULL, RTSP_EINVAL);

  sdp_media_uninit (media);
  g_free (media);

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
sdp_message_get_attribute_val_n (SDPMessage * msg, gchar * key, guint nth)
{
  guint i;

  for (i = 0; i < msg->attributes->len; i++) {
    SDPAttribute *attr;

    attr = &g_array_index (msg->attributes, SDPAttribute, i);
    if (!strcmp (attr->key, key)) {
      if (nth == 0)
        return attr->value;
      else
        nth--;
    }
  }
  return NULL;
}

gchar *
sdp_message_get_attribute_val (SDPMessage * msg, gchar * key)
{
  return sdp_message_get_attribute_val_n (msg, key, 0);
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

/**
 * sdp_message_add_media:
 * @msg: an #SDPMessage
 * @media: an #SDPMedia to add
 *
 * Adds @media to the array of medias in @msg. This function takes ownership of
 * the contents of @media so that @media will have to be reinitialized with
 * gst_media_init() before it can be used again.
 *
 * Returns: an #RTSPResult.
 */
RTSPResult
sdp_message_add_media (SDPMessage * msg, SDPMedia * media)
{
  gint len;
  SDPMedia *nmedia;

  len = msg->medias->len;
  g_array_set_size (msg->medias, len + 1);
  nmedia = &g_array_index (msg->medias, SDPMedia, len);

  memcpy (nmedia, media, sizeof (SDPMedia));
  memset (media, 0, sizeof (SDPMedia));

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

SDPAttribute *
sdp_media_get_attribute (SDPMedia * media, guint idx)
{
  return &g_array_index (media->attributes, SDPAttribute, idx);
}

gchar *
sdp_media_get_attribute_val_n (SDPMedia * media, gchar * key, guint nth)
{
  guint i;

  for (i = 0; i < media->attributes->len; i++) {
    SDPAttribute *attr;

    attr = &g_array_index (media->attributes, SDPAttribute, i);
    if (!strcmp (attr->key, key)) {
      if (nth == 0)
        return attr->value;
      else
        nth--;
    }
  }
  return NULL;
}

gchar *
sdp_media_get_attribute_val (SDPMedia * media, gchar * key)
{
  return sdp_media_get_attribute_val_n (media, key, 0);
}

gchar *
sdp_media_get_format (SDPMedia * media, guint idx)
{
  if (idx >= media->fmts->len)
    return NULL;
  return g_array_index (media->fmts, gchar *, idx);
}

static void
read_string (gchar * dest, guint size, gchar ** src)
{
  guint idx;

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
read_string_del (gchar * dest, guint size, gchar del, gchar ** src)
{
  guint idx;

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
  gchar str[8192];
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
      gchar str2[MAX_LINE_LEN];

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
      if (*p != '\0')
        p++;
      if (c->state == SDP_SESSION)
        sdp_message_add_attribute (c->msg, str, p);
      else
        sdp_media_add_attribute (c->media, str, p);
      break;
    case 'm':
    {
      gchar *slash;
      SDPMedia nmedia;

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
  gchar buffer[MAX_LINE_LEN];
  guint idx = 0;

  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, RTSP_EINVAL);
  g_return_val_if_fail (size != 0, RTSP_EINVAL);

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
    guint i;

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
    guint i;

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
  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);

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
    guint i;

    g_print (" emails:\n");
    for (i = 0; i < msg->emails->len; i++) {
      g_print ("  email '%s'\n", g_array_index (msg->emails, gchar *, i));
    }
  }
  if (msg->phones->len > 0) {
    guint i;

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
    guint i;

    g_print (" attributes:\n");
    for (i = 0; i < msg->attributes->len; i++) {
      SDPAttribute *attr = &g_array_index (msg->attributes, SDPAttribute, i);

      g_print ("  attribute '%s' : '%s'\n", attr->key, attr->value);
    }
  }
  if (msg->medias->len > 0) {
    guint i;

    g_print (" medias:\n");
    for (i = 0; i < msg->medias->len; i++) {
      g_print ("  media %d:\n", i);
      print_media (&g_array_index (msg->medias, SDPMedia, i));
    }
  }
  return RTSP_OK;
}
