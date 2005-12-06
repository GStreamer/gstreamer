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

#ifndef __RTSP_MESSAGE_H__
#define __RTSP_MESSAGE_H__

#include <glib.h>

#include <rtspdefs.h>

G_BEGIN_DECLS

typedef enum
{
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
    } request;
    struct {
      RTSPStatusCode     code;
      gchar             *reason;
    } response;
    struct {
      gint               channel;
    } data;
  } type_data;

  GHashTable    *hdr_fields;

  guint8        *body;
  guint          body_size;

} RTSPMessage;

RTSPResult      rtsp_message_new_request        (RTSPMethod method, gchar *uri, RTSPMessage **msg);
RTSPResult      rtsp_message_init_request       (RTSPMethod method, gchar *uri, RTSPMessage *msg);

RTSPResult      rtsp_message_new_response       (RTSPStatusCode code, gchar *reason, 
                                                 RTSPMessage *request, RTSPMessage **msg);
RTSPResult      rtsp_message_init_response      (RTSPStatusCode code, gchar *reason, 
                                                 RTSPMessage *request, RTSPMessage *msg);
RTSPResult      rtsp_message_init_data          (gint channel, RTSPMessage *msg);

RTSPResult      rtsp_message_free               (RTSPMessage *msg);


RTSPResult      rtsp_message_add_header         (RTSPMessage *msg, RTSPHeaderField field, gchar *value);
RTSPResult      rtsp_message_remove_header      (RTSPMessage *msg, RTSPHeaderField field);
RTSPResult      rtsp_message_get_header         (RTSPMessage *msg, RTSPHeaderField field, gchar **value);

RTSPResult      rtsp_message_set_body           (RTSPMessage *msg, guint8 *data, guint size);
RTSPResult      rtsp_message_take_body          (RTSPMessage *msg, guint8 *data, guint size);
RTSPResult      rtsp_message_get_body           (RTSPMessage *msg, guint8 **data, guint *size);

RTSPResult      rtsp_message_dump               (RTSPMessage *msg);

G_END_DECLS

#endif /* __RTSP_MESSAGE_H__ */
