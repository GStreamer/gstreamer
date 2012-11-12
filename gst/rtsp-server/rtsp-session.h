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

#include "rtsp-media.h"
#include "rtsp-session-media.h"

/**
 * GstRTSPSession:
 * @parent: the parent GObject
 * @sessionid: the session id of the session
 * @timeout: the timeout of the session
 * @create_time: the time when the session was created
 * @last_access: the time the session was last accessed
 * @expire_count: the expire prevention counter
 * @medias: a list of #GstRTSPSessionMedia managed in this session
 *
 * Session information kept by the server for a specific client.
 * One client session, identified with a session id, can handle multiple medias
 * identified with the url of a media.
 */
struct _GstRTSPSession {
  GObject       parent;

  gchar        *sessionid;

  guint         timeout;
  GTimeVal      create_time;
  GTimeVal      last_access;
  gint          expire_count;

  GList        *medias;
};

struct _GstRTSPSessionClass {
  GObjectClass  parent_class;
};

GType                  gst_rtsp_session_get_type             (void);

/* create a new session */
GstRTSPSession *       gst_rtsp_session_new                  (const gchar *sessionid);

const gchar *          gst_rtsp_session_get_sessionid        (GstRTSPSession *session);

gchar *                gst_rtsp_session_get_header           (GstRTSPSession *session);

void                   gst_rtsp_session_set_timeout          (GstRTSPSession *session, guint timeout);
guint                  gst_rtsp_session_get_timeout          (GstRTSPSession *session);

/* session timeout stuff */
void                   gst_rtsp_session_touch                (GstRTSPSession *session);
void                   gst_rtsp_session_prevent_expire       (GstRTSPSession *session);
void                   gst_rtsp_session_allow_expire         (GstRTSPSession *session);
gint                   gst_rtsp_session_next_timeout         (GstRTSPSession *session, GTimeVal *now);
gboolean               gst_rtsp_session_is_expired           (GstRTSPSession *session, GTimeVal *now);

/* handle media in a session */
GstRTSPSessionMedia *  gst_rtsp_session_manage_media         (GstRTSPSession *sess,
                                                              const GstRTSPUrl *uri,
                                                              GstRTSPMedia *media);
gboolean               gst_rtsp_session_release_media        (GstRTSPSession *sess,
                                                              GstRTSPSessionMedia *media);
/* get media in a session */
GstRTSPSessionMedia *  gst_rtsp_session_get_media            (GstRTSPSession *sess,
                                                              const GstRTSPUrl *url);

G_END_DECLS

#endif /* __GST_RTSP_SESSION_H__ */
