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

#include "rtspmessage.h"

RTSPResult
rtsp_message_new_request (RTSPMethod method, gchar * uri, RTSPMessage ** msg)
{
  RTSPMessage *newmsg;

  if (msg == NULL || uri == NULL)
    return RTSP_EINVAL;

  newmsg = g_new0 (RTSPMessage, 1);

  *msg = newmsg;

  return rtsp_message_init_request (method, uri, newmsg);
}

RTSPResult
rtsp_message_init_request (RTSPMethod method, gchar * uri, RTSPMessage * msg)
{
  if (msg == NULL || uri == NULL)
    return RTSP_EINVAL;

  msg->type = RTSP_MESSAGE_REQUEST;
  msg->type_data.request.method = method;
  g_free (msg->type_data.request.uri);
  msg->type_data.request.uri = g_strdup (uri);

  if (msg->hdr_fields != NULL)
    g_hash_table_destroy (msg->hdr_fields);
  msg->hdr_fields =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  if (msg->body) {
    g_free (msg->body);
    msg->body = NULL;
  }
  msg->body_size = 0;

  return RTSP_OK;
}

RTSPResult
rtsp_message_new_response (RTSPStatusCode code, gchar * reason,
    RTSPMessage * request, RTSPMessage ** msg)
{
  RTSPMessage *newmsg;

  if (msg == NULL || reason == NULL || request == NULL)
    return RTSP_EINVAL;

  newmsg = g_new0 (RTSPMessage, 1);

  *msg = newmsg;

  return rtsp_message_init_response (code, reason, request, newmsg);
}

RTSPResult
rtsp_message_init_response (RTSPStatusCode code, gchar * reason,
    RTSPMessage * request, RTSPMessage * msg)
{
  if (reason == NULL || msg == NULL)
    return RTSP_EINVAL;

  msg->type = RTSP_MESSAGE_RESPONSE;
  msg->type_data.response.code = code;
  g_free (msg->type_data.response.reason);
  msg->type_data.response.reason = g_strdup (reason);

  if (msg->hdr_fields != NULL)
    g_hash_table_destroy (msg->hdr_fields);
  msg->hdr_fields =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  if (msg->body) {
    g_free (msg->body);
    msg->body = NULL;
  }
  msg->body_size = 0;

  if (request) {
    /* FIXME copy headers */
  }

  return RTSP_OK;
}

RTSPResult
rtsp_message_init_data (gint channel, RTSPMessage * msg)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  msg->type = RTSP_MESSAGE_DATA;
  msg->type_data.data.channel = channel;

  return RTSP_OK;
}


RTSPResult
rtsp_message_add_header (RTSPMessage * msg, RTSPHeaderField field,
    gchar * value)
{
  if (msg == NULL || value == NULL)
    return RTSP_EINVAL;

  g_hash_table_insert (msg->hdr_fields, GINT_TO_POINTER (field),
      g_strdup (value));

  return RTSP_OK;
}

RTSPResult
rtsp_message_remove_header (RTSPMessage * msg, RTSPHeaderField field)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  g_hash_table_remove (msg->hdr_fields, GINT_TO_POINTER (field));

  return RTSP_ENOTIMPL;
}

RTSPResult
rtsp_message_get_header (RTSPMessage * msg, RTSPHeaderField field,
    gchar ** value)
{
  gchar *val;

  if (msg == NULL || value == NULL)
    return RTSP_EINVAL;

  val = g_hash_table_lookup (msg->hdr_fields, GINT_TO_POINTER (field));
  if (val == NULL)
    return RTSP_ENOTIMPL;

  *value = val;

  return RTSP_OK;
}

RTSPResult
rtsp_message_set_body (RTSPMessage * msg, guint8 * data, guint size)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  return rtsp_message_take_body (msg, g_memdup (data, size), size);
}

RTSPResult
rtsp_message_take_body (RTSPMessage * msg, guint8 * data, guint size)
{
  if (msg == NULL)
    return RTSP_EINVAL;

  if (msg->body)
    g_free (msg->body);

  msg->body = data;
  msg->body_size = size;

  return RTSP_OK;
}

RTSPResult
rtsp_message_get_body (RTSPMessage * msg, guint8 ** data, guint * size)
{
  if (msg == NULL || data == NULL || size == NULL)
    return RTSP_EINVAL;

  *data = msg->body;
  *size = msg->body_size;

  return RTSP_OK;
}

static void
dump_mem (guint8 * mem, gint size)
{
  guint i, j;
  GString *string = g_string_sized_new (50);
  GString *chars = g_string_sized_new (18);

  i = j = 0;
  while (i < size) {
    if (g_ascii_isprint (mem[i]))
      g_string_append_printf (chars, "%c", mem[i]);
    else
      g_string_append_printf (chars, ".");

    g_string_append_printf (string, "%02x ", mem[i]);

    j++;
    i++;

    if (j == 16 || i == size) {
      g_print ("%08x (%p): %-48.48s %-16.16s\n", i - j, mem + i - j,
          string->str, chars->str);
      g_string_set_size (string, 0);
      g_string_set_size (chars, 0);
      j = 0;
    }
  }
  g_string_free (string, TRUE);
  g_string_free (chars, TRUE);
}

static void
dump_key_value (gpointer key, gpointer value, gpointer data)
{
  RTSPHeaderField field = GPOINTER_TO_INT (key);

  g_print ("   key: '%s', value: '%s'\n", rtsp_header_as_text (field),
      (gchar *) value);
}

RTSPResult
rtsp_message_dump (RTSPMessage * msg)
{
  guint8 *data;
  guint size;

  if (msg == NULL)
    return RTSP_EINVAL;

  if (msg->type == RTSP_MESSAGE_REQUEST) {
    g_print ("request message %p\n", msg);
    g_print (" request line:\n");
    g_print ("   method: '%s'\n",
        rtsp_method_as_text (msg->type_data.request.method));
    g_print ("   uri:    '%s'\n", msg->type_data.request.uri);
    g_print (" headers:\n");
    g_hash_table_foreach (msg->hdr_fields, dump_key_value, NULL);
    g_print (" body:\n");
    rtsp_message_get_body (msg, &data, &size);
    dump_mem (data, size);
  } else if (msg->type == RTSP_MESSAGE_RESPONSE) {
    g_print ("response message %p\n", msg);
    g_print (" status line:\n");
    g_print ("   code:   '%d'\n", msg->type_data.response.code);
    g_print ("   reason: '%s'\n", msg->type_data.response.reason);
    g_print (" headers:\n");
    g_hash_table_foreach (msg->hdr_fields, dump_key_value, NULL);
    rtsp_message_get_body (msg, &data, &size);
    g_print (" body: length %d\n", size);
    dump_mem (data, size);
  } else if (msg->type == RTSP_MESSAGE_DATA) {
    g_print ("data message %p\n", msg);
    g_print (" channel: '%d'\n", msg->type_data.data.channel);
    g_print (" size:    '%d'\n", msg->body_size);
    rtsp_message_get_body (msg, &data, &size);
    dump_mem (data, size);
  } else {
    g_print ("unsupported packet type %d\n", msg->type);
    return RTSP_EINVAL;
  }
  return RTSP_OK;
}
