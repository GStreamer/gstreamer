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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/gst.h>
#include <gst/rtsp/gstrtsprange.h>
#include <gst/rtsp/gstrtspurl.h>

#ifndef __GST_RTSP_MEDIA_H__
#define __GST_RTSP_MEDIA_H__

G_BEGIN_DECLS

/* types for the media */
#define GST_TYPE_RTSP_MEDIA              (gst_rtsp_media_get_type ())
#define GST_IS_RTSP_MEDIA(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_MEDIA))
#define GST_IS_RTSP_MEDIA_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_MEDIA))
#define GST_RTSP_MEDIA_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_MEDIA, GstRTSPMediaClass))
#define GST_RTSP_MEDIA(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_MEDIA, GstRTSPMedia))
#define GST_RTSP_MEDIA_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_MEDIA, GstRTSPMediaClass))
#define GST_RTSP_MEDIA_CAST(obj)         ((GstRTSPMedia*)(obj))
#define GST_RTSP_MEDIA_CLASS_CAST(klass) ((GstRTSPMediaClass*)(klass))

typedef struct _GstRTSPMedia GstRTSPMedia;
typedef struct _GstRTSPMediaClass GstRTSPMediaClass;

#include "rtsp-stream.h"
#include "rtsp-auth.h"

/**
 * GstRTSPMediaStatus:
 * @GST_RTSP_MEDIA_STATUS_UNPREPARED: media pipeline not prerolled
 * @GST_RTSP_MEDIA_STATUS_UNPREPARING: media pipeline is busy doing a clean
 *                                     shutdown.
 * @GST_RTSP_MEDIA_STATUS_PREPARING: media pipeline is prerolling
 * @GST_RTSP_MEDIA_STATUS_PREPARED: media pipeline is prerolled
 * @GST_RTSP_MEDIA_STATUS_ERROR: media pipeline is in error
 *
 * The state of the media pipeline.
 */
typedef enum {
  GST_RTSP_MEDIA_STATUS_UNPREPARED  = 0,
  GST_RTSP_MEDIA_STATUS_UNPREPARING = 1,
  GST_RTSP_MEDIA_STATUS_PREPARING   = 2,
  GST_RTSP_MEDIA_STATUS_PREPARED    = 3,
  GST_RTSP_MEDIA_STATUS_ERROR       = 4
} GstRTSPMediaStatus;

/**
 * GstRTSPMedia:
 * @lock: for protecting the object
 * @cond: for signaling the object
 * @shared: if this media can be shared between clients
 * @reusable: if this media can be reused after an unprepare
 * @protocols: the allowed lower transport for this stream
 * @reused: if this media has been reused
 * @is_ipv6: if this media is using ipv6
 * @element: the data providing element
 * @streams: the different #GstRTSPStream provided by @element
 * @dynamic: list of dynamic elements managed by @element
 * @status: the status of the media pipeline
 * @n_active: the number of active connections
 * @pipeline: the toplevel pipeline
 * @fakesink: for making state changes async
 * @source: the bus watch for pipeline messages.
 * @id: the id of the watch
 * @is_live: if the pipeline is live
 * @seekable: if the pipeline can perform a seek
 * @buffering: if the pipeline is buffering
 * @target_state: the desired target state of the pipeline
 * @rtpbin: the rtpbin
 * @range: the range of the media being streamed
 *
 * A class that contains the GStreamer element along with a list of
 * #GstRTSPStream objects that can produce data.
 *
 * This object is usually created from a #GstRTSPMediaFactory.
 */
struct _GstRTSPMedia {
  GObject            parent;

  GMutex             lock;
  GCond              cond;

  gboolean           shared;
  gboolean           reusable;
  GstRTSPLowerTrans  protocols;
  gboolean           reused;
  gboolean           is_ipv6;
  gboolean           eos_shutdown;
  guint              buffer_size;
  GstRTSPAuth       *auth;
  gchar             *multicast_group;
  guint              mtu;

  GstElement        *element;
  GPtrArray         *streams;
  GList             *dynamic;
  GstRTSPMediaStatus status;
  gint               n_active;
  gboolean           adding;

  /* the pipeline for the media */
  GstElement        *pipeline;
  GstElement        *fakesink;
  GSource           *source;
  guint              id;

  gboolean           is_live;
  gboolean           seekable;
  gboolean           buffering;
  GstState           target_state;

  /* RTP session manager */
  GstElement        *rtpbin;

  /* the range of media */
  GstRTSPTimeRange   range;
};

/**
 * GstRTSPMediaClass:
 * @context: the main context for dispatching messages
 * @loop: the mainloop for message.
 * @thread: the thread dispatching messages.
 * @handle_message: handle a message
 * @unprepare: the default implementation sets the pipeline's state
 *             to GST_STATE_NULL.
 * @handle_mtu: handle a mtu
 *
 * The RTSP media class
 */
struct _GstRTSPMediaClass {
  GObjectClass  parent_class;

  /* thread for the mainloop */
  GMainContext *context;
  GMainLoop    *loop;
  GThread      *thread;

  /* vmethods */
  gboolean        (*handle_message)  (GstRTSPMedia *media, GstMessage *message);
  gboolean        (*unprepare)       (GstRTSPMedia *media);

  /* signals */
  gboolean        (*prepared)        (GstRTSPMedia *media);
  gboolean        (*unprepared)      (GstRTSPMedia *media);

  gboolean        (*new_state)       (GstRTSPMedia *media, GstState state);
};

GType                 gst_rtsp_media_get_type         (void);

/* creating the media */
GstRTSPMedia *        gst_rtsp_media_new              (void);

void                  gst_rtsp_media_set_shared       (GstRTSPMedia *media, gboolean shared);
gboolean              gst_rtsp_media_is_shared        (GstRTSPMedia *media);

void                  gst_rtsp_media_set_reusable     (GstRTSPMedia *media, gboolean reusable);
gboolean              gst_rtsp_media_is_reusable      (GstRTSPMedia *media);

void                  gst_rtsp_media_set_protocols    (GstRTSPMedia *media, GstRTSPLowerTrans protocols);
GstRTSPLowerTrans     gst_rtsp_media_get_protocols    (GstRTSPMedia *media);

void                  gst_rtsp_media_set_eos_shutdown (GstRTSPMedia *media, gboolean eos_shutdown);
gboolean              gst_rtsp_media_is_eos_shutdown  (GstRTSPMedia *media);

void                  gst_rtsp_media_set_auth         (GstRTSPMedia *media, GstRTSPAuth *auth);
GstRTSPAuth *         gst_rtsp_media_get_auth         (GstRTSPMedia *media);

void                  gst_rtsp_media_set_buffer_size  (GstRTSPMedia *media, guint size);
guint                 gst_rtsp_media_get_buffer_size  (GstRTSPMedia *media);

void                  gst_rtsp_media_set_multicast_group (GstRTSPMedia *media, const gchar * mc);
gchar *               gst_rtsp_media_get_multicast_group (GstRTSPMedia *media);

void                  gst_rtsp_media_set_mtu          (GstRTSPMedia *media, guint mtu);
guint                 gst_rtsp_media_get_mtu          (GstRTSPMedia *media);


/* prepare the media for playback */
gboolean              gst_rtsp_media_prepare          (GstRTSPMedia *media);
gboolean              gst_rtsp_media_is_prepared      (GstRTSPMedia *media);
gboolean              gst_rtsp_media_unprepare        (GstRTSPMedia *media);

/* creating streams */
void                  gst_rtsp_media_collect_streams  (GstRTSPMedia *media);
GstRTSPStream *       gst_rtsp_media_create_stream    (GstRTSPMedia *media,
                                                       GstElement *payloader,
                                                       GstPad *srcpad);

/* dealing with the media */
guint                 gst_rtsp_media_n_streams        (GstRTSPMedia *media);
GstRTSPStream *       gst_rtsp_media_get_stream       (GstRTSPMedia *media, guint idx);

gboolean              gst_rtsp_media_seek             (GstRTSPMedia *media, GstRTSPTimeRange *range);
gchar *               gst_rtsp_media_get_range_string (GstRTSPMedia *media, gboolean play);

gboolean              gst_rtsp_media_set_state        (GstRTSPMedia *media, GstState state,
                                                       GPtrArray *transports);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_H__ */
