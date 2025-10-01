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
#include "rtsp-server-prelude.h" /* for GST_RTSP_SERVER_DEPRECATED_FOR */

#ifndef __GST_RTSP_SESSION_H__
#define __GST_RTSP_SESSION_H__

G_BEGIN_DECLS

#define GST_TYPE_RTSP_SESSION              (gst_rtsp_session_get_type ())
#define GST_IS_RTSP_SESSION(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_SESSION))
#define GST_IS_RTSP_SESSION_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_SESSION))
#define GST_RTSP_SESSION_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_SESSION, GstRTSPSessionClass))
#define GST_RTSP_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_SESSION, GstRTSPSession))
#define GST_RTSP_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_SESSION, GstRTSPSessionClass))
#define GST_RTSP_SESSION_CAST(obj)         ((GstRTSPSession*)(obj))
#define GST_RTSP_SESSION_CLASS_CAST(klass) ((GstRTSPSessionClass*)(klass))

typedef struct _GstRTSPSession GstRTSPSession;
typedef struct _GstRTSPSessionClass GstRTSPSessionClass;
typedef struct _GstRTSPSessionPrivate GstRTSPSessionPrivate;

/**
 * GstRTSPFilterResult:
 * @GST_RTSP_FILTER_REMOVE: Remove session
 * @GST_RTSP_FILTER_KEEP: Keep session in the pool
 * @GST_RTSP_FILTER_REF: Ref session in the result list
 *
 * Possible return values for gst_rtsp_session_pool_filter().
 */
typedef enum
{
  GST_RTSP_FILTER_REMOVE,
  GST_RTSP_FILTER_KEEP,
  GST_RTSP_FILTER_REF,
} GstRTSPFilterResult;

#include "rtsp-media.h"
#include "rtsp-session-media.h"

/**
 * GstRTSPSession:
 *
 * Session information kept by the server for a specific client.
 * One client session, identified with a session id, can handle multiple medias
 * identified with the url of a media.
 */
struct _GstRTSPSession {
  GObject       parent;

  /*< private >*/
  GstRTSPSessionPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstRTSPSessionClass {
  GObjectClass  parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_RTSP_SERVER_API
GType                  gst_rtsp_session_get_type             (void);

/* create a new session */

GST_RTSP_SERVER_API
GstRTSPSession *       gst_rtsp_session_new                  (const gchar *sessionid) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
const gchar *          gst_rtsp_session_get_sessionid        (GstRTSPSession *session);

GST_RTSP_SERVER_API
gchar *                gst_rtsp_session_get_header           (GstRTSPSession *session) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
void                   gst_rtsp_session_set_timeout          (GstRTSPSession *session, guint timeout);

GST_RTSP_SERVER_API
guint                  gst_rtsp_session_get_timeout          (GstRTSPSession *session);

/* session timeout stuff */

GST_RTSP_SERVER_API
void                   gst_rtsp_session_touch                (GstRTSPSession *session);

GST_RTSP_SERVER_API
void                   gst_rtsp_session_prevent_expire       (GstRTSPSession *session);

GST_RTSP_SERVER_API
void                   gst_rtsp_session_allow_expire         (GstRTSPSession *session);

GST_RTSP_SERVER_API
gint                   gst_rtsp_session_next_timeout_usec    (GstRTSPSession *session, gint64 now);

GST_RTSP_SERVER_API
gboolean               gst_rtsp_session_is_expired_usec      (GstRTSPSession *session, gint64 now);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
GST_RTSP_SERVER_DEPRECATED_FOR(gst_rtsp_session_next_timeout_usec)
gint                   gst_rtsp_session_next_timeout         (GstRTSPSession *session, GTimeVal *now);

GST_RTSP_SERVER_DEPRECATED_FOR(gst_rtsp_session_is_expired_usec)
gboolean               gst_rtsp_session_is_expired           (GstRTSPSession *session, GTimeVal *now);
G_GNUC_END_IGNORE_DEPRECATIONS

/* handle media in a session */

GST_RTSP_SERVER_API
GstRTSPSessionMedia *  gst_rtsp_session_manage_media         (GstRTSPSession *sess,
                                                              const gchar *path,
                                                              GstRTSPMedia *media);

GST_RTSP_SERVER_API
gboolean               gst_rtsp_session_release_media        (GstRTSPSession *sess,
                                                              GstRTSPSessionMedia *media);
/* get media in a session */

GST_RTSP_SERVER_API
GstRTSPSessionMedia *  gst_rtsp_session_get_media            (GstRTSPSession *sess,
                                                              const gchar *path,
                                                              gint * matched);
/* get media in a session, increasing its reference count */

GST_RTSP_SERVER_API
GstRTSPSessionMedia *  gst_rtsp_session_dup_media            (GstRTSPSession *sess,
                                                              const gchar *path,
                                                              gint * matched) G_GNUC_WARN_UNUSED_RESULT;
/**
 * GstRTSPSessionFilterFunc:
 * @sess: a #GstRTSPSession object
 * @media: a #GstRTSPSessionMedia in @sess
 * @user_data: user data that has been given to gst_rtsp_session_filter()
 *
 * This function will be called by the gst_rtsp_session_filter(). An
 * implementation should return a value of #GstRTSPFilterResult.
 *
 * When this function returns #GST_RTSP_FILTER_REMOVE, @media will be removed
 * from @sess.
 *
 * A return value of #GST_RTSP_FILTER_KEEP will leave @media untouched in
 * @sess.
 *
 * A value of GST_RTSP_FILTER_REF will add @media to the result #GList of
 * gst_rtsp_session_filter().
 *
 * Returns: a #GstRTSPFilterResult.
 */
typedef GstRTSPFilterResult (*GstRTSPSessionFilterFunc)  (GstRTSPSession *sess,
                                                          GstRTSPSessionMedia *media,
                                                          gpointer user_data);

GST_RTSP_SERVER_API
GList *                gst_rtsp_session_filter           (GstRTSPSession *sess,
                                                          GstRTSPSessionFilterFunc func,
                                                          gpointer user_data) G_GNUC_WARN_UNUSED_RESULT;


#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstRTSPSession, gst_object_unref)
#endif

G_END_DECLS

#endif /* __GST_RTSP_SESSION_H__ */
