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

#ifndef __GST_RTSP_SESSION_POOL_H__
#define __GST_RTSP_SESSION_POOL_H__

#include "rtsp-session.h"

G_BEGIN_DECLS

#define GST_TYPE_RTSP_SESSION_POOL              (gst_rtsp_session_pool_get_type ())
#define GST_IS_RTSP_SESSION_POOL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_SESSION_POOL))
#define GST_IS_RTSP_SESSION_POOL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_SESSION_POOL))
#define GST_RTSP_SESSION_POOL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_SESSION_POOL, GstRTSPSessionPoolClass))
#define GST_RTSP_SESSION_POOL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_SESSION_POOL, GstRTSPSessionPool))
#define GST_RTSP_SESSION_POOL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_SESSION_POOL, GstRTSPSessionPoolClass))
#define GST_RTSP_SESSION_POOL_CAST(obj)         ((GstRTSPSessionPool*)(obj))
#define GST_RTSP_SESSION_POOL_CLASS_CAST(klass) ((GstRTSPSessionPoolClass*)(klass))

typedef struct _GstRTSPSessionPool GstRTSPSessionPool;
typedef struct _GstRTSPSessionPoolClass GstRTSPSessionPoolClass;

/**
 * GstRTSPSessionPool:
 * @max_sessions: the maximum number of sessions.
 * @lock: locking the session hashtable
 * @session: hashtable of sessions indexed by the session id.
 *
 * An object that keeps track of the active sessions. This object is usually
 * attached to a #GstRTSPServer object to manage the sessions in that server.
 */
struct _GstRTSPSessionPool {
  GObject       parent;

  guint         max_sessions;

  GMutex       *lock;
  GHashTable   *sessions;
};

/**
 * GstRTSPSessionPoolClass:
 * @create_session_id: create a new random session id. Subclasses can create
 *    custom session ids and should not check if the session exists.
 */
struct _GstRTSPSessionPoolClass {
  GObjectClass  parent_class;

  gchar * (*create_session_id)   (GstRTSPSessionPool *pool);
};

/**
 * GstRTSPSessionPoolFunc:
 * @pool: a #GstRTSPSessionPool object
 * @user_data: user data that has been given when registering the handler
 *
 * The function that will be called from the GSource watch on the session pool.
 *
 * The function will be called when the pool must be cleaned up because one or
 * more sessions timed out.
 *
 * Returns: %FALSe if the source should be removed.
 */
typedef gboolean (*GstRTSPSessionPoolFunc)  (GstRTSPSessionPool *pool, gpointer user_data);

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

/**
 * GstRTSPSessionFilterFunc:
 * @pool: a #GstRTSPSessionPool object
 * @session: a #GstRTSPSession in @pool
 * @user_data: user data that has been given to gst_rtsp_session_pool_filter()
 *
 * This function will be called by the gst_rtsp_session_pool_filter(). An
 * implementation should return a value of #GstRTSPFilterResult.
 *
 * When this function returns #GST_RTSP_FILTER_REMOVE, @session will be removed
 * from @pool.
 *
 * A return value of #GST_RTSP_FILTER_KEEP will leave @session untouched in
 * @pool.
 *
 * A value of GST_RTSP_FILTER_REF will add @session to the result #GList of
 * gst_rtsp_session_pool_filter().
 *
 * Returns: a #GstRTSPFilterResult.
 */
typedef GstRTSPFilterResult (*GstRTSPSessionFilterFunc)  (GstRTSPSessionPool *pool, 
                                                          GstRTSPSession *session,
                                                          gpointer user_data);


GType                 gst_rtsp_session_pool_get_type          (void);

/* creating a session pool */
GstRTSPSessionPool *  gst_rtsp_session_pool_new               (void);

/* counting sessionss */
void                  gst_rtsp_session_pool_set_max_sessions  (GstRTSPSessionPool *pool, guint max);
guint                 gst_rtsp_session_pool_get_max_sessions  (GstRTSPSessionPool *pool);

guint                 gst_rtsp_session_pool_get_n_sessions    (GstRTSPSessionPool *pool);

/* managing sessions */
GstRTSPSession *      gst_rtsp_session_pool_create            (GstRTSPSessionPool *pool);
GstRTSPSession *      gst_rtsp_session_pool_find              (GstRTSPSessionPool *pool,
                                                               const gchar *sessionid);
gboolean              gst_rtsp_session_pool_remove            (GstRTSPSessionPool *pool,
                                                               GstRTSPSession *sess);

/* perform session maintenance */
GList *               gst_rtsp_session_pool_filter            (GstRTSPSessionPool *pool,
                                                               GstRTSPSessionFilterFunc func,
							       gpointer user_data);
guint                 gst_rtsp_session_pool_cleanup           (GstRTSPSessionPool *pool);
GSource *             gst_rtsp_session_pool_create_watch      (GstRTSPSessionPool *pool);

G_END_DECLS

#endif /* __GST_RTSP_SESSION_POOL_H__ */
