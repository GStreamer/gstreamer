/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
 *               <2006> Lutz Mueller <lutz at topfrose dot de>
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

#include <string.h>

#include "gstrtspmessage.h"

typedef struct _RTSPKeyValue
{
  GstRTSPHeaderField field;
  gchar *value;
} RTSPKeyValue;

static void
key_value_foreach (GArray * array, GFunc func, gpointer user_data)
{
  guint i;

  g_return_if_fail (array != NULL);

  for (i = 0; i < array->len; i++) {
    (*func) (&g_array_index (array, RTSPKeyValue, i), user_data);
  }
}

GstRTSPResult
gst_rtsp_message_new (GstRTSPMessage ** msg)
{
  GstRTSPMessage *newmsg;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  newmsg = g_new0 (GstRTSPMessage, 1);

  *msg = newmsg;

  return gst_rtsp_message_init (newmsg);
}

GstRTSPResult
gst_rtsp_message_init (GstRTSPMessage * msg)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  gst_rtsp_message_unset (msg);

  msg->type = GST_RTSP_MESSAGE_INVALID;
  msg->hdr_fields = g_array_new (FALSE, FALSE, sizeof (RTSPKeyValue));

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_new_request (GstRTSPMessage ** msg, GstRTSPMethod method,
    const gchar * uri)
{
  GstRTSPMessage *newmsg;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (uri != NULL, GST_RTSP_EINVAL);

  newmsg = g_new0 (GstRTSPMessage, 1);

  *msg = newmsg;

  return gst_rtsp_message_init_request (newmsg, method, uri);
}

GstRTSPResult
gst_rtsp_message_init_request (GstRTSPMessage * msg, GstRTSPMethod method,
    const gchar * uri)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (uri != NULL, GST_RTSP_EINVAL);

  gst_rtsp_message_unset (msg);

  msg->type = GST_RTSP_MESSAGE_REQUEST;
  msg->type_data.request.method = method;
  msg->type_data.request.uri = g_strdup (uri);
  msg->type_data.request.version = GST_RTSP_VERSION_1_0;
  msg->hdr_fields = g_array_new (FALSE, FALSE, sizeof (RTSPKeyValue));

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_new_response (GstRTSPMessage ** msg, GstRTSPStatusCode code,
    const gchar * reason, const GstRTSPMessage * request)
{
  GstRTSPMessage *newmsg;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  newmsg = g_new0 (GstRTSPMessage, 1);

  *msg = newmsg;

  return gst_rtsp_message_init_response (newmsg, code, reason, request);
}

GstRTSPResult
gst_rtsp_message_init_response (GstRTSPMessage * msg, GstRTSPStatusCode code,
    const gchar * reason, const GstRTSPMessage * request)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  gst_rtsp_message_unset (msg);

  if (reason == NULL)
    reason = gst_rtsp_status_as_text (code);

  msg->type = GST_RTSP_MESSAGE_RESPONSE;
  msg->type_data.response.code = code;
  msg->type_data.response.reason = g_strdup (reason);
  msg->type_data.response.version = GST_RTSP_VERSION_1_0;
  msg->hdr_fields = g_array_new (FALSE, FALSE, sizeof (RTSPKeyValue));

  if (request) {
    gchar *header;

    /* copy CSEQ */
    if (gst_rtsp_message_get_header (request, GST_RTSP_HDR_CSEQ, &header,
            0) == GST_RTSP_OK) {
      gst_rtsp_message_add_header (msg, GST_RTSP_HDR_CSEQ, header);
    }

    /* copy session id */
    if (gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &header,
            0) == GST_RTSP_OK) {
      char *pos;

      header = g_strdup (header);
      if ((pos = strchr (header, ';'))) {
        *pos = '\0';
      }
      g_strchomp (header);
      gst_rtsp_message_add_header (msg, GST_RTSP_HDR_SESSION, header);
      g_free (header);
    }

    /* FIXME copy more headers? */
  }

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_init_data (GstRTSPMessage * msg, guint8 channel)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  gst_rtsp_message_unset (msg);

  msg->type = GST_RTSP_MESSAGE_DATA;
  msg->type_data.data.channel = channel;

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_unset (GstRTSPMessage * msg)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  switch (msg->type) {
    case GST_RTSP_MESSAGE_INVALID:
      break;
    case GST_RTSP_MESSAGE_REQUEST:
      g_free (msg->type_data.request.uri);
      break;
    case GST_RTSP_MESSAGE_RESPONSE:
      g_free (msg->type_data.response.reason);
      break;
    case GST_RTSP_MESSAGE_DATA:
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (msg->hdr_fields != NULL)
    g_array_free (msg->hdr_fields, TRUE);

  g_free (msg->body);

  memset (msg, 0, sizeof *msg);

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_free (GstRTSPMessage * msg)
{
  GstRTSPResult res;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  res = gst_rtsp_message_unset (msg);
  if (res == GST_RTSP_OK)
    g_free (msg);

  return res;
}

GstRTSPResult
gst_rtsp_message_add_header (GstRTSPMessage * msg, GstRTSPHeaderField field,
    const gchar * value)
{
  RTSPKeyValue key_value;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (value != NULL, GST_RTSP_EINVAL);

  key_value.field = field;
  key_value.value = g_strdup (value);

  g_array_append_val (msg->hdr_fields, key_value);


  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_remove_header (GstRTSPMessage * msg, GstRTSPHeaderField field,
    gint indx)
{
  GstRTSPResult res = GST_RTSP_ENOTIMPL;
  guint i = 0;
  gint cnt = 0;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  while (i < msg->hdr_fields->len) {
    RTSPKeyValue key_value = g_array_index (msg->hdr_fields, RTSPKeyValue, i);

    if (key_value.field == field && (indx == -1 || cnt++ == indx)) {
      g_array_remove_index (msg->hdr_fields, i);
      res = GST_RTSP_OK;
      if (indx != -1)
        break;
    } else {
      i++;
    }
  }

  return res;
}

GstRTSPResult
gst_rtsp_message_get_header (const GstRTSPMessage * msg,
    GstRTSPHeaderField field, gchar ** value, gint indx)
{
  guint i;
  gint cnt = 0;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  for (i = 0; i < msg->hdr_fields->len; i++) {
    RTSPKeyValue key_value = g_array_index (msg->hdr_fields, RTSPKeyValue, i);

    if (key_value.field == field && cnt++ == indx) {
      if (value)
        *value = key_value.value;
      return GST_RTSP_OK;
    }
  }

  return GST_RTSP_ENOTIMPL;
}

GstRTSPResult
gst_rtsp_message_append_headers (const GstRTSPMessage * msg, GString * str)
{
  guint i;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (str != NULL, GST_RTSP_EINVAL);

  for (i = 0; i < msg->hdr_fields->len; i++) {
    RTSPKeyValue key_value = g_array_index (msg->hdr_fields, RTSPKeyValue, i);
    const gchar *keystr = gst_rtsp_header_as_text (key_value.field);

    g_string_append_printf (str, "%s: %s\r\n", keystr, key_value.value);
  }
  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_set_body (GstRTSPMessage * msg, const guint8 * data,
    guint size)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  return gst_rtsp_message_take_body (msg, g_memdup (data, size), size);
}

GstRTSPResult
gst_rtsp_message_take_body (GstRTSPMessage * msg, guint8 * data, guint size)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL || size == 0, GST_RTSP_EINVAL);

  if (msg->body)
    g_free (msg->body);

  msg->body = data;
  msg->body_size = size;

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_get_body (const GstRTSPMessage * msg, guint8 ** data,
    guint * size)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (size != NULL, GST_RTSP_EINVAL);

  *data = msg->body;
  *size = msg->body_size;

  return GST_RTSP_OK;
}

GstRTSPResult
gst_rtsp_message_steal_body (GstRTSPMessage * msg, guint8 ** data, guint * size)
{
  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (size != NULL, GST_RTSP_EINVAL);

  *data = msg->body;
  *size = msg->body_size;

  msg->body = NULL;
  msg->body_size = 0;

  return GST_RTSP_OK;
}

static void
dump_mem (guint8 * mem, guint size)
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
dump_key_value (gpointer data, gpointer user_data)
{
  RTSPKeyValue *key_value = (RTSPKeyValue *) data;

  g_print ("   key: '%s', value: '%s'\n",
      gst_rtsp_header_as_text (key_value->field), key_value->value);
}

GstRTSPResult
gst_rtsp_message_dump (GstRTSPMessage * msg)
{
  guint8 *data;
  guint size;

  g_return_val_if_fail (msg != NULL, GST_RTSP_EINVAL);

  switch (msg->type) {
    case GST_RTSP_MESSAGE_REQUEST:
      g_print ("RTSP request message %p\n", msg);
      g_print (" request line:\n");
      g_print ("   method: '%s'\n",
          gst_rtsp_method_as_text (msg->type_data.request.method));
      g_print ("   uri:    '%s'\n", msg->type_data.request.uri);
      g_print ("   version: '%s'\n",
          gst_rtsp_version_as_text (msg->type_data.request.version));
      g_print (" headers:\n");
      key_value_foreach (msg->hdr_fields, dump_key_value, NULL);
      g_print (" body:\n");
      gst_rtsp_message_get_body (msg, &data, &size);
      dump_mem (data, size);
      break;
    case GST_RTSP_MESSAGE_RESPONSE:
      g_print ("RTSP response message %p\n", msg);
      g_print (" status line:\n");
      g_print ("   code:   '%d'\n", msg->type_data.response.code);
      g_print ("   reason: '%s'\n", msg->type_data.response.reason);
      g_print ("   version: '%s'\n",
          gst_rtsp_version_as_text (msg->type_data.response.version));
      g_print (" headers:\n");
      key_value_foreach (msg->hdr_fields, dump_key_value, NULL);
      gst_rtsp_message_get_body (msg, &data, &size);
      g_print (" body: length %d\n", size);
      dump_mem (data, size);
      break;
    case GST_RTSP_MESSAGE_DATA:
      g_print ("RTSP data message %p\n", msg);
      g_print (" channel: '%d'\n", msg->type_data.data.channel);
      g_print (" size:    '%d'\n", msg->body_size);
      gst_rtsp_message_get_body (msg, &data, &size);
      dump_mem (data, size);
      break;
    default:
      g_print ("unsupported message type %d\n", msg->type);
      return GST_RTSP_EINVAL;
  }
  return GST_RTSP_OK;
}
