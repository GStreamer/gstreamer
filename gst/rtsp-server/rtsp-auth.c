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
/**
 * SECTION:rtsp-auth
 * @short_description: Authentication and authorization
 * @see_also: #GstRTSPPermissions, #GstRTSPToken
 *
 * The #GstRTSPAuth object is responsible for checking if the current user is
 * allowed to perform requested actions. The default implementation has some
 * reasonable checks but subclasses can implement custom security policies.
 *
 * A new auth object is made with gst_rtsp_auth_new(). It is usually configured
 * on the #GstRTSPServer object.
 *
 * The RTSP server will call gst_rtsp_auth_check() with a string describing the
 * check to perform. The possible checks are prefixed with
 * GST_RTSP_AUTH_CHECK_*. Depending on the check, the default implementation
 * will use the current #GstRTSPToken, #GstRTSPContext and
 * #GstRTSPPermissions on the object to check if an operation is allowed.
 *
 * The default #GstRTSPAuth object has support for basic authentication. With
 * gst_rtsp_auth_add_basic() you can add a basic authentication string together
 * with the #GstRTSPToken that will become active when successfully
 * authenticated.
 *
 * When a TLS certificate has been set with gst_rtsp_auth_set_tls_certificate(),
 * the default auth object will require the client to connect with a TLS
 * connection.
 *
 * Last reviewed on 2013-07-16 (1.0.0)
 */

#include <string.h>

#include "rtsp-auth.h"

#define GST_RTSP_AUTH_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_AUTH, GstRTSPAuthPrivate))

struct _GstRTSPAuthPrivate
{
  GMutex lock;

  /* the TLS certificate */
  GTlsCertificate *certificate;
  GHashTable *basic;            /* protected by lock */
  GstRTSPToken *default_token;
  GstRTSPMethod methods;
};

enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_auth_debug);
#define GST_CAT_DEFAULT rtsp_auth_debug

static void gst_rtsp_auth_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_auth_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_auth_finalize (GObject * obj);

static gboolean default_authenticate (GstRTSPAuth * auth, GstRTSPContext * ctx);
static gboolean default_check (GstRTSPAuth * auth, GstRTSPContext * ctx,
    const gchar * check);

G_DEFINE_TYPE (GstRTSPAuth, gst_rtsp_auth, G_TYPE_OBJECT);

static void
gst_rtsp_auth_class_init (GstRTSPAuthClass * klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (GstRTSPAuthPrivate));

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_auth_get_property;
  gobject_class->set_property = gst_rtsp_auth_set_property;
  gobject_class->finalize = gst_rtsp_auth_finalize;

  klass->authenticate = default_authenticate;
  klass->check = default_check;

  GST_DEBUG_CATEGORY_INIT (rtsp_auth_debug, "rtspauth", 0, "GstRTSPAuth");
}

static void
gst_rtsp_auth_init (GstRTSPAuth * auth)
{
  GstRTSPAuthPrivate *priv;

  auth->priv = priv = GST_RTSP_AUTH_GET_PRIVATE (auth);

  g_mutex_init (&priv->lock);

  priv->basic = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) gst_rtsp_token_unref);

  /* bitwise or of all methods that need authentication */
  priv->methods = 0;
}

static void
gst_rtsp_auth_finalize (GObject * obj)
{
  GstRTSPAuth *auth = GST_RTSP_AUTH (obj);
  GstRTSPAuthPrivate *priv = auth->priv;

  GST_INFO ("finalize auth %p", auth);

  if (priv->certificate)
    g_object_unref (priv->certificate);
  g_hash_table_unref (priv->basic);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_auth_parent_class)->finalize (obj);
}

static void
gst_rtsp_auth_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  switch (propid) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_auth_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  switch (propid) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_auth_new:
 *
 * Create a new #GstRTSPAuth instance.
 *
 * Returns: (transfer full): a new #GstRTSPAuth
 */
GstRTSPAuth *
gst_rtsp_auth_new (void)
{
  GstRTSPAuth *result;

  result = g_object_new (GST_TYPE_RTSP_AUTH, NULL);

  return result;
}

/**
 * gst_rtsp_auth_set_tls_certificate:
 * @auth: a #GstRTSPAuth
 * @cert: (transfer none) (allow-none): a #GTlsCertificate
 *
 * Set the TLS certificate for the auth. Client connections will only
 * be accepted when TLS is negotiated.
 */
void
gst_rtsp_auth_set_tls_certificate (GstRTSPAuth * auth, GTlsCertificate * cert)
{
  GstRTSPAuthPrivate *priv;
  GTlsCertificate *old;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));

  priv = auth->priv;

  if (cert)
    g_object_ref (cert);

  g_mutex_lock (&priv->lock);
  old = priv->certificate;
  priv->certificate = cert;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_auth_get_tls_certificate:
 * @auth: a #GstRTSPAuth
 *
 * Get the #GTlsCertificate used for negotiating TLS @auth.
 *
 * Returns: (transfer full): the #GTlsCertificate of @auth. g_object_unref() after
 * usage.
 */
GTlsCertificate *
gst_rtsp_auth_get_tls_certificate (GstRTSPAuth * auth)
{
  GstRTSPAuthPrivate *priv;
  GTlsCertificate *result;

  g_return_val_if_fail (GST_IS_RTSP_AUTH (auth), NULL);

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->certificate))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_auth_set_default_token:
 * @auth: a #GstRTSPAuth
 * @token: (transfer none) (allow-none): a #GstRTSPToken
 *
 * Set the default #GstRTSPToken to @token in @auth. The default token will
 * be used for unauthenticated users.
 */
void
gst_rtsp_auth_set_default_token (GstRTSPAuth * auth, GstRTSPToken * token)
{
  GstRTSPAuthPrivate *priv;
  GstRTSPToken *old;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));

  priv = auth->priv;

  if (token)
    gst_rtsp_token_ref (token);

  g_mutex_lock (&priv->lock);
  old = priv->default_token;
  priv->default_token = token;
  g_mutex_unlock (&priv->lock);

  if (old)
    gst_rtsp_token_unref (old);
}

/**
 * gst_rtsp_auth_get_default_token:
 * @auth: a #GstRTSPAuth
 *
 * Get the default token for @auth. This token will be used for unauthenticated
 * users.
 *
 * Returns: (transfer full): the #GstRTSPToken of @auth. gst_rtsp_token_unref() after
 * usage.
 */
GstRTSPToken *
gst_rtsp_auth_get_default_token (GstRTSPAuth * auth)
{
  GstRTSPAuthPrivate *priv;
  GstRTSPToken *result;

  g_return_val_if_fail (GST_IS_RTSP_AUTH (auth), NULL);

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->default_token))
    gst_rtsp_token_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_auth_add_basic:
 * @auth: a #GstRTSPAuth
 * @basic: the basic token
 * @token: (transfer none): authorisation token
 *
 * Add a basic token for the default authentication algorithm that
 * enables the client with privileges listed in @token.
 */
void
gst_rtsp_auth_add_basic (GstRTSPAuth * auth, const gchar * basic,
    GstRTSPToken * token)
{
  GstRTSPAuthPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));
  g_return_if_fail (basic != NULL);
  g_return_if_fail (GST_IS_RTSP_TOKEN (token));

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  g_hash_table_replace (priv->basic, g_strdup (basic),
      gst_rtsp_token_ref (token));
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_auth_remove_basic:
 * @auth: a #GstRTSPAuth
 * @basic: (transfer none): the basic token
 *
 * Add a basic token for the default authentication algorithm that
 * enables the client with privileges from @authgroup.
 */
void
gst_rtsp_auth_remove_basic (GstRTSPAuth * auth, const gchar * basic)
{
  GstRTSPAuthPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));
  g_return_if_fail (basic != NULL);

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  g_hash_table_remove (priv->basic, basic);
  g_mutex_unlock (&priv->lock);
}

static gboolean
default_authenticate (GstRTSPAuth * auth, GstRTSPContext * ctx)
{
  GstRTSPAuthPrivate *priv = auth->priv;
  GstRTSPResult res;
  gchar *authorization;

  GST_DEBUG_OBJECT (auth, "authenticate");

  g_mutex_lock (&priv->lock);
  /* FIXME, need to ref but we have no way to unref when the ctx is
   * popped */
  ctx->token = priv->default_token;
  g_mutex_unlock (&priv->lock);

  res =
      gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_AUTHORIZATION,
      &authorization, 0);
  if (res < 0)
    goto no_auth;

  /* parse type */
  if (g_ascii_strncasecmp (authorization, "basic ", 6) == 0) {
    GstRTSPToken *token;

    GST_DEBUG_OBJECT (auth, "check Basic auth");
    g_mutex_lock (&priv->lock);
    if ((token = g_hash_table_lookup (priv->basic, &authorization[6]))) {
      GST_DEBUG_OBJECT (auth, "setting token %p", token);
      ctx->token = token;
    }
    g_mutex_unlock (&priv->lock);
  } else if (g_ascii_strncasecmp (authorization, "digest ", 7) == 0) {
    GST_DEBUG_OBJECT (auth, "check Digest auth");
    /* not implemented yet */
  }
  return TRUE;

no_auth:
  {
    GST_DEBUG_OBJECT (auth, "no authorization header found");
    return TRUE;
  }
}

static void
send_response (GstRTSPAuth * auth, GstRTSPStatusCode code, GstRTSPContext * ctx)
{
  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  if (code == GST_RTSP_STS_UNAUTHORIZED) {
    /* we only have Basic for now */
    gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_WWW_AUTHENTICATE,
        "Basic realm=\"GStreamer RTSP Server\"");
  }
  gst_rtsp_client_send_message (ctx->client, ctx->session, ctx->response);
}

static gboolean
ensure_authenticated (GstRTSPAuth * auth, GstRTSPContext * ctx)
{
  GstRTSPAuthClass *klass;

  klass = GST_RTSP_AUTH_GET_CLASS (auth);

  /* we need a token to check */
  if (ctx->token == NULL) {
    if (klass->authenticate) {
      if (!klass->authenticate (auth, ctx))
        goto authenticate_failed;
    }
  }
  if (ctx->token == NULL)
    goto no_auth;

  return TRUE;

/* ERRORS */
authenticate_failed:
  {
    GST_DEBUG_OBJECT (auth, "authenticate failed");
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, ctx);
    return FALSE;
  }
no_auth:
  {
    GST_DEBUG_OBJECT (auth, "no authorization token found");
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, ctx);
    return FALSE;
  }
}

/* new connection */
static gboolean
check_connect (GstRTSPAuth * auth, GstRTSPContext * ctx, const gchar * check)
{
  GstRTSPAuthPrivate *priv = auth->priv;

  if (priv->certificate) {
    GTlsConnection *tls;

    /* configure the connection */
    tls = gst_rtsp_connection_get_tls (ctx->conn, NULL);
    g_tls_connection_set_certificate (tls, priv->certificate);
  }
  return TRUE;
}

/* check url and methods */
static gboolean
check_url (GstRTSPAuth * auth, GstRTSPContext * ctx, const gchar * check)
{
  GstRTSPAuthPrivate *priv = auth->priv;

  if ((ctx->method & priv->methods) != 0)
    if (!ensure_authenticated (auth, ctx))
      goto not_authenticated;

  return TRUE;

  /* ERRORS */
not_authenticated:
  {
    return FALSE;
  }
}

/* check access to media factory */
static gboolean
check_factory (GstRTSPAuth * auth, GstRTSPContext * ctx, const gchar * check)
{
  const gchar *role;
  GstRTSPPermissions *perms;

  if (!ensure_authenticated (auth, ctx))
    return FALSE;

  if (!(role = gst_rtsp_token_get_string (ctx->token,
              GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE)))
    goto no_media_role;
  if (!(perms = gst_rtsp_media_factory_get_permissions (ctx->factory)))
    goto no_permissions;

  if (g_str_equal (check, GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_ACCESS)) {
    if (!gst_rtsp_permissions_is_allowed (perms, role,
            GST_RTSP_PERM_MEDIA_FACTORY_ACCESS))
      goto no_access;
  } else if (g_str_equal (check, GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_CONSTRUCT)) {
    if (!gst_rtsp_permissions_is_allowed (perms, role,
            GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT))
      goto no_construct;
  }

  gst_rtsp_permissions_unref (perms);

  return TRUE;

  /* ERRORS */
no_media_role:
  {
    GST_DEBUG_OBJECT (auth, "no media factory role found");
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, ctx);
    return FALSE;
  }
no_permissions:
  {
    GST_DEBUG_OBJECT (auth, "no permissions on media factory found");
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, ctx);
    return FALSE;
  }
no_access:
  {
    GST_DEBUG_OBJECT (auth, "no permissions to access media factory");
    gst_rtsp_permissions_unref (perms);
    send_response (auth, GST_RTSP_STS_NOT_FOUND, ctx);
    return FALSE;
  }
no_construct:
  {
    GST_DEBUG_OBJECT (auth, "no permissions to construct media factory");
    gst_rtsp_permissions_unref (perms);
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, ctx);
    return FALSE;
  }
}

static gboolean
check_client_settings (GstRTSPAuth * auth, GstRTSPContext * ctx,
    const gchar * check)
{
  if (!ensure_authenticated (auth, ctx))
    return FALSE;

  return gst_rtsp_token_is_allowed (ctx->token,
      GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS);
}

static gboolean
default_check (GstRTSPAuth * auth, GstRTSPContext * ctx, const gchar * check)
{
  gboolean res = FALSE;

  /* FIXME, use hastable or so */
  if (g_str_equal (check, GST_RTSP_AUTH_CHECK_CONNECT)) {
    res = check_connect (auth, ctx, check);
  } else if (g_str_equal (check, GST_RTSP_AUTH_CHECK_URL)) {
    res = check_url (auth, ctx, check);
  } else if (g_str_has_prefix (check, "auth.check.media.factory.")) {
    res = check_factory (auth, ctx, check);
  } else if (g_str_equal (check, GST_RTSP_AUTH_CHECK_TRANSPORT_CLIENT_SETTINGS)) {
    res = check_client_settings (auth, ctx, check);
  }
  return res;
}

static gboolean
no_auth_check (const gchar * check)
{
  gboolean res;

  if (g_str_equal (check, GST_RTSP_AUTH_CHECK_TRANSPORT_CLIENT_SETTINGS))
    res = FALSE;
  else
    res = TRUE;

  return res;
}

/**
 * gst_rtsp_auth_check:
 * @check: the item to check
 *
 * Check if @check is allowed in the current context.
 *
 * Returns: FALSE if check failed.
 */
gboolean
gst_rtsp_auth_check (const gchar * check)
{
  gboolean result = FALSE;
  GstRTSPAuthClass *klass;
  GstRTSPContext *ctx;
  GstRTSPAuth *auth;

  g_return_val_if_fail (check != NULL, FALSE);

  if (!(ctx = gst_rtsp_context_get_current ()))
    goto no_context;

  /* no auth, we don't need to check */
  if (!(auth = ctx->auth))
    return no_auth_check (check);

  klass = GST_RTSP_AUTH_GET_CLASS (auth);

  GST_DEBUG_OBJECT (auth, "check authorization '%s'", check);

  if (klass->check)
    result = klass->check (auth, ctx, check);

  return result;

  /* ERRORS */
no_context:
  {
    GST_ERROR ("no context found");
    return FALSE;
  }
}

/**
 * gst_rtsp_auth_make_basic:
 * @user: a userid
 * @pass: a password
 *
 * Construct a Basic authorisation token from @user and @pass.
 *
 * Returns: (transfer full): the base64 encoding of the string @user:@pass.
 * g_free() after usage.
 */
gchar *
gst_rtsp_auth_make_basic (const gchar * user, const gchar * pass)
{
  gchar *user_pass;
  gchar *result;

  g_return_val_if_fail (user != NULL, NULL);
  g_return_val_if_fail (pass != NULL, NULL);

  user_pass = g_strjoin (":", user, pass, NULL);
  result = g_base64_encode ((guchar *) user_pass, strlen (user_pass));
  g_free (user_pass);

  return result;
}
