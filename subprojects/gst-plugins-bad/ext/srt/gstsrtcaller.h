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

#ifndef __GST_SRT_CALLER_H__
#define __GST_SRT_CALLER_H__

#include <gio/gio.h>
#include <srt/srt.h>

G_BEGIN_DECLS

typedef struct
{
  SRTSOCKET sock;
  gint poll_id;
  GSocketAddress *sockaddr;
  gboolean sent_headers;
} GstSRTCaller;

GstSRTCaller *  gst_srt_caller_new (SRTSOCKET sock, gint poll_id, GSocketAddress *sockaddr);
void            gst_srt_caller_close (GstSRTCaller *caller);

G_END_DECLS

#endif // __GST_SRT_CALLER_H__
