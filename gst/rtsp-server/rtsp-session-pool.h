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
 *
 * @lock: locking the session hashtable
 * @session: hashtable of sessions indexed by the session id.
 *
 * An object that keeps track of the active sessions.
 */
struct _GstRTSPSessionPool {
  GObject       parent;

  GMutex       *lock;
  GHashTable   *sessions;
};

/**
 * GstRTSPSessionPoolClass:
 *
 * @create_session_id: create a new random session id. Subclasses should not
 * check if the session exists.
 */
struct _GstRTSPSessionPoolClass {
  GObjectClass  parent_class;

  gchar * (*create_session_id)   (GstRTSPSessionPool *pool);
};

GType                 gst_rtsp_session_pool_get_type          (void);

GstRTSPSessionPool *  gst_rtsp_session_pool_new               (void);

GstRTSPSession *      gst_rtsp_session_pool_find              (GstRTSPSessionPool *pool,
                                                               const gchar *sessionid);
GstRTSPSession *      gst_rtsp_session_pool_create            (GstRTSPSessionPool *pool);
void                  gst_rtsp_session_pool_remove            (GstRTSPSessionPool *pool,
                                                               GstRTSPSession *sess);

G_END_DECLS

#endif /* __GST_RTSP_SESSION_POOL_H__ */
