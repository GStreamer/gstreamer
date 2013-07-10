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
#include <gst/rtsp/gstrtsprange.h>
#include <gst/rtsp/gstrtspurl.h>
#include <gst/net/gstnet.h>

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
typedef struct _GstRTSPMediaPrivate GstRTSPMediaPrivate;

#include "rtsp-stream.h"
#include "rtsp-thread-pool.h"
#include "rtsp-permissions.h"
#include "rtsp-address-pool.h"

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
 *
 * A class that contains the GStreamer element along with a list of
 * #GstRTSPStream objects that can produce data.
 *
 * This object is usually created from a #GstRTSPMediaFactory.
 */
struct _GstRTSPMedia {
  GObject            parent;

  GstRTSPMediaPrivate *priv;
};

/**
 * GstRTSPMediaClass:
 * @context: the main context for dispatching messages
 * @loop: the mainloop for message.
 * @thread: the thread dispatching messages.
 * @handle_message: handle a message
 * @unprepare: the default implementation sets the pipeline's state
 *             to GST_STATE_NULL and removes all elements.
 *
 * The RTSP media class
 */
struct _GstRTSPMediaClass {
  GObjectClass  parent_class;

  /* vmethods */
  gboolean        (*handle_message)  (GstRTSPMedia *media, GstMessage *message);
  gboolean        (*unprepare)       (GstRTSPMedia *media);
  gboolean        (*convert_range)   (GstRTSPMedia *media, GstRTSPTimeRange *range,
                                      GstRTSPRangeUnit unit);
  gboolean        (*query_position)  (GstRTSPMedia *media, gint64 *position);
  gboolean        (*query_stop)      (GstRTSPMedia *media, gint64 *stop);

  /* signals */
  void            (*new_stream)      (GstRTSPMedia *media, GstRTSPStream * stream);
  void            (*removed_stream)  (GstRTSPMedia *media, GstRTSPStream * stream);

  void            (*prepared)        (GstRTSPMedia *media);
  void            (*unprepared)      (GstRTSPMedia *media);

  void            (*new_state)       (GstRTSPMedia *media, GstState state);
};

GType                 gst_rtsp_media_get_type         (void);

/* creating the media */
GstRTSPMedia *        gst_rtsp_media_new              (GstElement *element);
GstElement *          gst_rtsp_media_get_element      (GstRTSPMedia *media);

void                  gst_rtsp_media_take_pipeline    (GstRTSPMedia *media, GstPipeline *pipeline);

GstRTSPMediaStatus    gst_rtsp_media_get_status       (GstRTSPMedia *media);

void                  gst_rtsp_media_set_permissions  (GstRTSPMedia *media,
                                                       GstRTSPPermissions *permissions);
GstRTSPPermissions *  gst_rtsp_media_get_permissions  (GstRTSPMedia *media);

void                  gst_rtsp_media_set_shared       (GstRTSPMedia *media, gboolean shared);
gboolean              gst_rtsp_media_is_shared        (GstRTSPMedia *media);

void                  gst_rtsp_media_set_reusable     (GstRTSPMedia *media, gboolean reusable);
gboolean              gst_rtsp_media_is_reusable      (GstRTSPMedia *media);

void                  gst_rtsp_media_set_protocols    (GstRTSPMedia *media, GstRTSPLowerTrans protocols);
GstRTSPLowerTrans     gst_rtsp_media_get_protocols    (GstRTSPMedia *media);

void                  gst_rtsp_media_set_eos_shutdown (GstRTSPMedia *media, gboolean eos_shutdown);
gboolean              gst_rtsp_media_is_eos_shutdown  (GstRTSPMedia *media);

void                  gst_rtsp_media_set_address_pool (GstRTSPMedia *media, GstRTSPAddressPool *pool);
GstRTSPAddressPool *  gst_rtsp_media_get_address_pool (GstRTSPMedia *media);

void                  gst_rtsp_media_set_buffer_size  (GstRTSPMedia *media, guint size);
guint                 gst_rtsp_media_get_buffer_size  (GstRTSPMedia *media);

void                  gst_rtsp_media_use_time_provider (GstRTSPMedia *media, gboolean time_provider);
gboolean              gst_rtsp_media_is_time_provider  (GstRTSPMedia *media);
GstNetTimeProvider *  gst_rtsp_media_get_time_provider (GstRTSPMedia *media,
                                                        const gchar *address, guint16 port);

/* prepare the media for playback */
gboolean              gst_rtsp_media_prepare          (GstRTSPMedia *media, GstRTSPThread *thread);
gboolean              gst_rtsp_media_unprepare        (GstRTSPMedia *media);

/* creating streams */
void                  gst_rtsp_media_collect_streams  (GstRTSPMedia *media);
GstRTSPStream *       gst_rtsp_media_create_stream    (GstRTSPMedia *media,
                                                       GstElement *payloader,
                                                       GstPad *srcpad);

/* dealing with the media */
GstClock *            gst_rtsp_media_get_clock        (GstRTSPMedia *media);
GstClockTime          gst_rtsp_media_get_base_time    (GstRTSPMedia *media);

guint                 gst_rtsp_media_n_streams        (GstRTSPMedia *media);
GstRTSPStream *       gst_rtsp_media_get_stream       (GstRTSPMedia *media, guint idx);
GstRTSPStream *       gst_rtsp_media_find_stream      (GstRTSPMedia *media, const gchar * control);

gboolean              gst_rtsp_media_seek             (GstRTSPMedia *media, GstRTSPTimeRange *range);
gchar *               gst_rtsp_media_get_range_string (GstRTSPMedia *media,
                                                       gboolean play,
                                                       GstRTSPRangeUnit unit);

gboolean              gst_rtsp_media_set_state        (GstRTSPMedia *media, GstState state,
                                                       GPtrArray *transports);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_H__ */
