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

#ifndef __RTSP_MESSAGE_H__
#define __RTSP_MESSAGE_H__

#include <glib.h>

#include <rtspdefs.h>

G_BEGIN_DECLS

typedef enum
{
  RTSP_MESSAGE_INVALID,
  RTSP_MESSAGE_REQUEST,
  RTSP_MESSAGE_RESPONSE,
  RTSP_MESSAGE_DATA,
} RTSPMsgType;

typedef struct _RTSPMessage
{
  RTSPMsgType    type;

  union {
    struct {
      RTSPMethod         method;
      gchar             *uri;
      RTSPVersion        version;
    } request;
    struct {
      RTSPStatusCode     code;
      gchar             *reason;
      RTSPVersion        version;
    } response;
    struct {
      guint8             channel;
    } data;
  } type_data;

  GArray        *hdr_fields;

  guint8        *body;
  guint          body_size;

} RTSPMessage;

RTSPResult      rtsp_message_new                (RTSPMessage **msg);
RTSPResult      rtsp_message_init               (RTSPMessage *msg);

RTSPResult      rtsp_message_new_request        (RTSPMessage **msg,
                                                 RTSPMethod method,
                                                 const gchar *uri);
RTSPResult      rtsp_message_init_request       (RTSPMessage *msg,
                                                 RTSPMethod method,
                                                 const gchar *uri);

RTSPResult      rtsp_message_new_response       (RTSPMessage **msg,
                                                 RTSPStatusCode code,
                                                 const gchar *reason,
                                                 const RTSPMessage *request);
RTSPResult      rtsp_message_init_response      (RTSPMessage *msg,
                                                 RTSPStatusCode code,
                                                 const gchar *reason,
                                                 const RTSPMessage *request);

RTSPResult      rtsp_message_init_data          (RTSPMessage *msg,
                                                 guint8 channel);

RTSPResult      rtsp_message_unset              (RTSPMessage *msg);
RTSPResult      rtsp_message_free               (RTSPMessage *msg);


RTSPResult      rtsp_message_add_header         (RTSPMessage *msg,
                                                 RTSPHeaderField field,
                                                 const gchar *value);
RTSPResult      rtsp_message_remove_header      (RTSPMessage *msg,
                                                 RTSPHeaderField field,
                                                 gint indx);
RTSPResult      rtsp_message_get_header         (const RTSPMessage *msg,
                                                 RTSPHeaderField field,
                                                 gchar **value,
                                                 gint indx);

void            rtsp_message_append_headers     (const RTSPMessage *msg,
                                                 GString *str);

RTSPResult      rtsp_message_set_body           (RTSPMessage *msg,
                                                 const guint8 *data,
                                                 guint size);
RTSPResult      rtsp_message_take_body          (RTSPMessage *msg,
                                                 guint8 *data,
                                                 guint size);
RTSPResult      rtsp_message_get_body           (const RTSPMessage *msg,
                                                 guint8 **data,
                                                 guint *size);
RTSPResult      rtsp_message_steal_body         (RTSPMessage *msg,
                                                 guint8 **data,
                                                 guint *size);

RTSPResult      rtsp_message_dump               (RTSPMessage *msg);

G_END_DECLS

#endif /* __RTSP_MESSAGE_H__ */
