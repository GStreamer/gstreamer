/* GStreamer
 * Copyright (C) 2018, Collabora Ltd.
 * Copyright (C) 2018, SK Telecom, Co., Ltd.
 *   Author: Jeongseok Kim <jeongseok.kim@sk.com>
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

#ifndef __GST_SRT_OBJECT_H__
#define __GST_SRT_OBJECT_H__

#include "gstsrt-enums.h"
#include "gstsrt-enumtypes.h"

#include <gio/gio.h>
#include <srt/srt.h>

G_BEGIN_DECLS

#define GST_SRT_DEFAULT_URI_SCHEME "srt"
#define GST_SRT_DEFAULT_PORT 7001
#define GST_SRT_DEFAULT_HOST "127.0.0.1"
#define GST_SRT_DEFAULT_LOCALADDRESS "0.0.0.0"
#define GST_SRT_DEFAULT_URI GST_SRT_DEFAULT_URI_SCHEME"://"GST_SRT_DEFAULT_HOST":"G_STRINGIFY(GST_SRT_DEFAULT_PORT)

#define GST_SRT_DEFAULT_MODE GST_SRT_CONNECTION_MODE_CALLER
#define GST_SRT_DEFAULT_PBKEYLEN GST_SRT_KEY_LENGTH_0
#define GST_SRT_DEFAULT_POLL_TIMEOUT -1
#define GST_SRT_DEFAULT_LATENCY 125
#define GST_SRT_DEFAULT_MSG_SIZE 1316
#define GST_SRT_DEFAULT_WAIT_FOR_CONNECTION (TRUE)

typedef struct _GstSRTObject GstSRTObject;

struct _GstSRTObject
{
  GstElement                   *element;
  GstUri                       *uri;

  GstStructure                 *parameters;
  gboolean                      opened;
  SRTSOCKET                     sock;
  gint                          poll_id;
  gboolean                      sent_headers;

  GTask                        *listener_task;
  SRTSOCKET                     listener_sock;
  gint                          listener_poll_id;

  GThread                      *thread;

  /* Protects the list of callers */
  GMutex                        sock_lock;
  GCond                         sock_cond;

  GList                        *callers;

  gboolean                     wait_for_connection;

  guint64                      previous_bytes;
};

GstSRTObject   *gst_srt_object_new              (GstElement *element);

void            gst_srt_object_destroy          (GstSRTObject *srtobject);

gboolean        gst_srt_object_open             (GstSRTObject *srtobject,
                                                 GCancellable *cancellable,
                                                 GError **error);

void            gst_srt_object_close            (GstSRTObject *srtobject);

gboolean        gst_srt_object_set_property_helper (GstSRTObject *srtobject,
                                                    guint prop_id, const GValue * value,
                                                    GParamSpec * pspec);

gboolean        gst_srt_object_get_property_helper (GstSRTObject *srtobject,
                                                    guint prop_id, GValue * value,
                                                    GParamSpec * pspec);

void            gst_srt_object_install_properties_helper (GObjectClass *gobject_class);

gboolean        gst_srt_object_set_uri (GstSRTObject * srtobject, const gchar *uri, GError ** err);

gssize          gst_srt_object_read     (GstSRTObject * srtobject,
                                         guint8 *data, gsize size,
                                         GCancellable *cancellable,
                                         GError **err,
					 SRT_MSGCTRL *mctrl);

gssize          gst_srt_object_write    (GstSRTObject * srtobject,
                                         GstBufferList * headers,
                                         const GstMapInfo * mapinfo,
                                         GCancellable *cancellable,
                                         GError **err);

void            gst_srt_object_wakeup   (GstSRTObject * srtobject,
                                         GCancellable *cancellable);

GstStructure   *gst_srt_object_get_stats        (GstSRTObject * srtobject);

G_END_DECLS

#endif // __GST_SRT_OBJECT_H__
