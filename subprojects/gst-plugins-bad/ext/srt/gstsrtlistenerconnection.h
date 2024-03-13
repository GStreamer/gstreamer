/* GStreamer
 *   Author: Jonas Danielsson <jonas.danielsson@spiideo,com>
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
 */

#ifndef __GST_SRT_LISTENER_CONNECTION_H__
#define __GST_SRT_LISTENER_CONNECTION_H__

#include <glib.h>
#include <srt/srt.h>

#include "gstsrtobject.h"

G_BEGIN_DECLS
#define SRT_CONNECTION_ELEMENT_ERROR(connection, domain, code, text, debug) \
G_STMT_START {                                                              \
  for (GList *iter = connection->objects; iter; iter = iter->next) {        \
    GstSRTObject *object = iter->data;                                      \
    GST_ELEMENT_ERROR(object->element, domain, code, text, debug);          \
  }                                                                         \
} G_STMT_END

typedef struct
{
  SRTSOCKET sock;
  SRTSOCKET rsock;
  gint poll_id;
  GList *objects;
  int poll_timeout;
  gboolean initialized;
  gboolean key_is_set;
  char *key;
  GThread *accept_thread;

} GstSRTListenerConnection;

gboolean                   gst_srt_listener_connection_add_object      (GstSRTObject * srtobject, GError ** error);
gboolean                   gst_srt_listener_connection_remove_object   (GstSRTObject * srtobject, GError ** error);


G_END_DECLS
#endif // __GST_SRT_LISTENER_CONNECTION_H__
