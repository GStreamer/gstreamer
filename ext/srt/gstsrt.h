/* GStreamer
 * Copyright (C) 2017, Collabora Ltd.
 *   Author: Olivier Crete <olivier.crete@collabora.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SRT_H__
#define __GST_SRT_H__

#include <gst/gst.h>
#include <gio/gio.h>
#include <gio/gnetworking.h>

#include <srt/srt.h>

#define SRT_URI_SCHEME "srt"
#define SRT_DEFAULT_PORT 7001
#define SRT_DEFAULT_HOST "127.0.0.1"
#define SRT_DEFAULT_URI SRT_URI_SCHEME"://"SRT_DEFAULT_HOST":"G_STRINGIFY(SRT_DEFAULT_PORT)
#define SRT_DEFAULT_LATENCY 125
#define SRT_DEFAULT_KEY_LENGTH 16

G_BEGIN_DECLS

SRTSOCKET
gst_srt_client_connect (GstElement * elem, int sender,
    const gchar * host, guint16 port, int rendez_vous,
    const gchar * bind_address, guint16 bind_port, int latency,
    GSocketAddress ** socket_address, gint * poll_id);

SRTSOCKET
gst_srt_client_connect_full (GstElement * elem, int sender,
    const gchar * host, guint16 port, int rendez_vous,
    const gchar * bind_address, guint16 bind_port, int latency,
    GSocketAddress ** socket_address, gint * poll_id,
    gchar * passphrase, int key_length);

G_END_DECLS


#endif /* __GST_SRT_H__ */
