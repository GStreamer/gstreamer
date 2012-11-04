/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include <gst/gst.h>

#include <gst/rtsp/gstrtsptransport.h>

#ifndef __GST_RTSP_SESSION_MEDIA_H__
#define __GST_RTSP_SESSION_MEDIA_H__

G_BEGIN_DECLS

#define GST_TYPE_RTSP_SESSION_MEDIA              (gst_rtsp_session_media_get_type ())
#define GST_IS_RTSP_SESSION_MEDIA(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_SESSION_MEDIA))
#define GST_IS_RTSP_SESSION_MEDIA_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_SESSION_MEDIA))
#define GST_RTSP_SESSION_MEDIA_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_SESSION_MEDIA, GstRTSPSessionMediaClass))
#define GST_RTSP_SESSION_MEDIA(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_SESSION_MEDIA, GstRTSPSessionMedia))
#define GST_RTSP_SESSION_MEDIA_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_SESSION_MEDIA, GstRTSPSessionMediaClass))
#define GST_RTSP_SESSION_MEDIA_CAST(obj)         ((GstRTSPSessionMedia*)(obj))
#define GST_RTSP_SESSION_MEDIA_CLASS_CAST(klass) ((GstRTSPSessionMediaClass*)(klass))

typedef struct _GstRTSPSessionMedia GstRTSPSessionMedia;
typedef struct _GstRTSPSessionMediaClass GstRTSPSessionMediaClass;

/**
 * GstRTSPSessionMedia:
 * @url: the url of the media
 * @media: the pipeline for the media
 * @state: the server state
 * @counter: counter for channels
 * @transports: array of #GstRTSPStreamTransport with the configuration
 *   for the transport for each selected stream from @media.
 *
 * State of a client session regarding a specific media identified by uri.
 */
struct _GstRTSPSessionMedia
{
  GObject  parent;

  GstRTSPUrl   *url;
  GstRTSPMedia *media;
  GstRTSPState  state;
  guint         counter;

  GPtrArray    *transports;
};

struct _GstRTSPSessionMediaClass
{
  GObjectClass  parent_class;
};

GType                    gst_rtsp_session_media_get_type       (void);

GstRTSPSessionMedia *    gst_rtsp_session_media_new            (const GstRTSPUrl *url,
                                                                GstRTSPMedia *media);
/* control media */
gboolean                 gst_rtsp_session_media_set_state      (GstRTSPSessionMedia *media,
                                                                GstState state);

/* get stream transport config */
GstRTSPStreamTransport * gst_rtsp_session_media_set_transport  (GstRTSPSessionMedia *media,
                                                                GstRTSPStream *stream,
                                                                GstRTSPTransport *tr);
GstRTSPStreamTransport * gst_rtsp_session_media_get_transport  (GstRTSPSessionMedia *media,
                                                                guint idx);

gboolean                 gst_rtsp_session_media_alloc_channels (GstRTSPSessionMedia *media,
                                                                GstRTSPRange *range);

G_END_DECLS

#endif /* __GST_RTSP_SESSION_MEDIA_H__ */
