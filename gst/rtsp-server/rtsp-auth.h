/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans at gmail.com>
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

#ifndef __GST_RTSP_AUTH_H__
#define __GST_RTSP_AUTH_H__

typedef struct _GstRTSPAuth GstRTSPAuth;
typedef struct _GstRTSPAuthClass GstRTSPAuthClass;
typedef struct _GstRTSPAuthPrivate GstRTSPAuthPrivate;

#include "rtsp-client.h"
#include "rtsp-token.h"

G_BEGIN_DECLS

#define GST_TYPE_RTSP_AUTH              (gst_rtsp_auth_get_type ())
#define GST_IS_RTSP_AUTH(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_AUTH))
#define GST_IS_RTSP_AUTH_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_AUTH))
#define GST_RTSP_AUTH_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_AUTH, GstRTSPAuthClass))
#define GST_RTSP_AUTH(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_AUTH, GstRTSPAuth))
#define GST_RTSP_AUTH_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_AUTH, GstRTSPAuthClass))
#define GST_RTSP_AUTH_CAST(obj)         ((GstRTSPAuth*)(obj))
#define GST_RTSP_AUTH_CLASS_CAST(klass) ((GstRTSPAuthClass*)(klass))

/**
 * GstRTSPAuth:
 *
 * The authentication structure.
 */
struct _GstRTSPAuth {
  GObject       parent;

  GstRTSPAuthPrivate *priv;
};

/**
 * GstRTSPAuthClass:
 * @setup: called when an unauthorized resource has been accessed and
 *         authentication needs to be requested to the client. The default
 *         implementation adds basic authentication to the response.
 * @authenticate: check the authentication of a client. The default implementation
 *         checks if the authentication in the header matches one of the basic
 *         authentication tokens. This function should set the authgroup field
 *         in the state.
 * @check: check if a resource can be accessed. this function should
 *         call validate to authenticate the client when needed. The default
 *         implementation disallows unauthenticated access to all methods
 *         except OPTIONS.
 *
 * The authentication class.
 */
struct _GstRTSPAuthClass {
  GObjectClass  parent_class;

  gboolean           (*setup)        (GstRTSPAuth *auth, GstRTSPClientState *state);
  gboolean           (*authenticate) (GstRTSPAuth *auth, GstRTSPClientState *state);
  gboolean           (*check)        (GstRTSPAuth *auth, GstRTSPClientState *state,
                                      const gchar *check);
};

GType               gst_rtsp_auth_get_type          (void);

GstRTSPAuth *       gst_rtsp_auth_new               (void);

void                gst_rtsp_auth_add_basic         (GstRTSPAuth *auth, const gchar * basic,
                                                     GstRTSPToken *token);
void                gst_rtsp_auth_remove_basic      (GstRTSPAuth *auth, const gchar * basic);

gboolean            gst_rtsp_auth_setup             (GstRTSPAuth *auth, GstRTSPClientState *state);

gboolean            gst_rtsp_auth_check             (const gchar *check);


/* helpers */
gchar *             gst_rtsp_auth_make_basic        (const gchar * user, const gchar * pass);

/* checks */
/**
 * GST_RTSP_AUTH_CHECK_URL:
 *
 * Check the URL and methods
 */
#define GST_RTSP_AUTH_CHECK_URL                      "auth.check.url"
/**
 * GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_ACCESS:
 *
 * Check if access is allowed to a factory
 */
#define GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_ACCESS     "auth.check.media.factory.access"
/**
 * GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_CONSTRUCT:
 *
 * Check if media can be constructed from a media factory
 */
#define GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_CONSTRUCT  "auth.check.media.factory.construct"


/* tokens */
/**
 * GST_RTSP_MEDIA_FACTORY_ROLE:
 *
 * G_TYPE_STRING, the role to use when dealing with media factories
 */
#define GST_RTSP_MEDIA_FACTORY_ROLE      "media.factory.role"

/* permissions */
/**
 * GST_RTSP_MEDIA_FACTORY_PERM_ACCESS:
 *
 * G_TYPE_BOOLEAN, %TRUE if the media can be accessed, %FALSE will
 * return a 404 Not Found error when trying to access the media.
 */
#define GST_RTSP_MEDIA_FACTORY_PERM_ACCESS      "media.factory.access"
/**
 * GST_RTSP_MEDIA_FACTORY_PERM_CONSTRUCT:
 *
 * G_TYPE_BOOLEAN, %TRUE if the media can be constructed, %FALSE will
 * return a 404 Not Found error when trying to access the media.
 */
#define GST_RTSP_MEDIA_FACTORY_PERM_CONSTRUCT   "media.factory.construct"


G_END_DECLS

#endif /* __GST_RTSP_AUTH_H__ */
