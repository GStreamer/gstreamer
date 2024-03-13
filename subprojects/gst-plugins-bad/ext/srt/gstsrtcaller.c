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

#include "gstsrtcaller.h"

GstSRTCaller *
gst_srt_caller_new (SRTSOCKET sock, gint poll_id, GSocketAddress * sockaddr)
{
  GstSRTCaller *caller = g_new0 (GstSRTCaller, 1);
  caller->sock = sock;
  caller->poll_id = poll_id;
  caller->sockaddr = sockaddr;
  caller->sent_headers = FALSE;

  return caller;
}

void
gst_srt_caller_close (GstSRTCaller * caller)
{
  g_return_if_fail (caller != NULL);

  g_clear_object (&caller->sockaddr);
  if (caller->sock != SRT_INVALID_SOCK) {
    srt_close (caller->sock);
  }
  if (caller->poll_id != SRT_ERROR) {
    srt_epoll_release (caller->poll_id);
  }
  g_free (caller);
}
