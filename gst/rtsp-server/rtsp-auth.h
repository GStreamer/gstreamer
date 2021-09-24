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

#include "rtsp-server-prelude.h"
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

  /*< private >*/
  GstRTSPAuthPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstRTSPAuthClass:
 * @authenticate: check the authentication of a client. The default implementation
 *         checks if the authentication in the header matches one of the basic
 *         authentication tokens. This function should set the authgroup field
 *         in the context.
 * @check: check if a resource can be accessed. this function should
 *         call authenticate to authenticate the client when needed. The method
 *         should also construct and send an appropriate response message on
 *         error.
 *
 * The authentication class.
 */
struct _GstRTSPAuthClass {
  GObjectClass  parent_class;

  gboolean           (*authenticate) (GstRTSPAuth *auth, GstRTSPContext *ctx);
  gboolean           (*check)        (GstRTSPAuth *auth, GstRTSPContext *ctx,
                                      const gchar *check);
  void               (*generate_authenticate_header) (GstRTSPAuth *auth, GstRTSPContext *ctx);
  gboolean           (*accept_certificate) (GstRTSPAuth *auth,
                                            GTlsConnection *connection,
                                            GTlsCertificate *peer_cert,
                                            GTlsCertificateFlags errors);
  /*< private >*/
  gpointer            _gst_reserved[GST_PADDING - 1];
};

GST_RTSP_SERVER_API
GType               gst_rtsp_auth_get_type          (void);

GST_RTSP_SERVER_API
GstRTSPAuth *       gst_rtsp_auth_new               (void);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_set_tls_certificate (GstRTSPAuth *auth, GTlsCertificate *cert);

GST_RTSP_SERVER_API
GTlsCertificate *   gst_rtsp_auth_get_tls_certificate (GstRTSPAuth *auth);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_set_tls_database (GstRTSPAuth *auth, GTlsDatabase *database);

GST_RTSP_SERVER_API
GTlsDatabase *      gst_rtsp_auth_get_tls_database (GstRTSPAuth *auth);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_set_tls_authentication_mode (GstRTSPAuth *auth, GTlsAuthenticationMode mode);

GST_RTSP_SERVER_API
GTlsAuthenticationMode gst_rtsp_auth_get_tls_authentication_mode (GstRTSPAuth *auth);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_set_default_token (GstRTSPAuth *auth, GstRTSPToken *token);

GST_RTSP_SERVER_API
GstRTSPToken *      gst_rtsp_auth_get_default_token (GstRTSPAuth *auth);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_add_basic         (GstRTSPAuth *auth, const gchar * basic,
                                                     GstRTSPToken *token);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_remove_basic      (GstRTSPAuth *auth, const gchar * basic);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_add_digest        (GstRTSPAuth *auth, const gchar *user,
                                                     const gchar *pass, GstRTSPToken *token);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_remove_digest     (GstRTSPAuth *auth, const gchar *user);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_set_supported_methods (GstRTSPAuth *auth, GstRTSPAuthMethod methods);

GST_RTSP_SERVER_API
GstRTSPAuthMethod   gst_rtsp_auth_get_supported_methods (GstRTSPAuth *auth);

GST_RTSP_SERVER_API
gboolean            gst_rtsp_auth_check             (const gchar *check);

GST_RTSP_SERVER_API
gboolean            gst_rtsp_auth_parse_htdigest    (GstRTSPAuth *auth, const gchar *path, GstRTSPToken *token);

GST_RTSP_SERVER_API
void                gst_rtsp_auth_set_realm         (GstRTSPAuth *auth, const gchar *realm);

GST_RTSP_SERVER_API
gchar *             gst_rtsp_auth_get_realm         (GstRTSPAuth *auth);

/* helpers */

GST_RTSP_SERVER_API
gchar *             gst_rtsp_auth_make_basic        (const gchar * user, const gchar * pass);

/* checks */
/**
 * GST_RTSP_AUTH_CHECK_CONNECT:
 *
 * Check a new connection
 */
#define GST_RTSP_AUTH_CHECK_CONNECT                  "auth.check.connect"
/**
 * GST_RTSP_AUTH_CHECK_URL:
 *
 * Check the URL and methods
 */
#define GST_RTSP_AUTH_CHECK_URL                      "auth.check.url"
/**
 * GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_ACCESS:
 *
 * Check if access is allowed to a factory.
 * When access is not allowed an 404 Not Found is sent in the response.
 */
#define GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_ACCESS     "auth.check.media.factory.access"
/**
 * GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_CONSTRUCT:
 *
 * Check if media can be constructed from a media factory
 * A response should be sent on error.
 */
#define GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_CONSTRUCT  "auth.check.media.factory.construct"
/**
 * GST_RTSP_AUTH_CHECK_TRANSPORT_CLIENT_SETTINGS:
 *
 * Check if the client can specify TTL, destination and
 * port pair in multicast. No response is sent when the check returns
 * %FALSE.
 */
#define GST_RTSP_AUTH_CHECK_TRANSPORT_CLIENT_SETTINGS  "auth.check.transport.client-settings"


/* tokens */
/**
 * GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE:
 *
 * G_TYPE_STRING, the role to use when dealing with media factories
 *
 * The default #GstRTSPAuth object uses this string in the token to find the
 * role of the media factory. It will then retrieve the #GstRTSPPermissions of
 * the media factory and retrieve the role with the same name.
 */
#define GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE      "media.factory.role"
/**
 * GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS:
 *
 * G_TYPE_BOOLEAN, %TRUE if the client can specify TTL, destination and
 *     port pair in multicast.
 */
#define GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS   "transport.client-settings"

/* permissions */
/**
 * GST_RTSP_PERM_MEDIA_FACTORY_ACCESS:
 *
 * G_TYPE_BOOLEAN, %TRUE if the media can be accessed, %FALSE will
 * return a 404 Not Found error when trying to access the media.
 */
#define GST_RTSP_PERM_MEDIA_FACTORY_ACCESS      "media.factory.access"
/**
 * GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT:
 *
 * G_TYPE_BOOLEAN, %TRUE if the media can be constructed, %FALSE will
 * return a 404 Not Found error when trying to access the media.
 */
#define GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT   "media.factory.construct"

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstRTSPAuth, gst_object_unref)
#endif

G_END_DECLS

#endif /* __GST_RTSP_AUTH_H__ */
