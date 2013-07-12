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
 * @see_also: #GstRTSPPermission, #GstRTSPtoken
 *
 * Last reviewed on 2013-07-11 (1.0.0)
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

static gboolean default_authenticate (GstRTSPAuth * auth,
    GstRTSPClientState * state);
static gboolean default_check (GstRTSPAuth * auth, GstRTSPClientState * state,
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
  priv->methods = GST_RTSP_DESCRIBE |
      GST_RTSP_ANNOUNCE |
      GST_RTSP_GET_PARAMETER |
      GST_RTSP_SET_PARAMETER |
      GST_RTSP_PAUSE |
      GST_RTSP_PLAY | GST_RTSP_RECORD | GST_RTSP_SETUP | GST_RTSP_TEARDOWN;
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
 * Returns: a new #GstRTSPAuth
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
 * @cert: (allow none): a #GTlsCertificate
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
 * gst_rtsp_auth_add_basic:
 * @auth: a #GstRTSPAuth
 * @basic: the basic token
 * @token: authorisation token
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
 * enables the client qith privileges from @authgroup.
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
default_authenticate (GstRTSPAuth * auth, GstRTSPClientState * state)
{
  GstRTSPAuthPrivate *priv = auth->priv;
  GstRTSPResult res;
  gchar *authorization;

  GST_DEBUG_OBJECT (auth, "authenticate");

  res =
      gst_rtsp_message_get_header (state->request, GST_RTSP_HDR_AUTHORIZATION,
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
      state->token = token;
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

static gboolean
ensure_authenticated (GstRTSPAuth * auth, GstRTSPClientState * state)
{
  GstRTSPAuthClass *klass;

  klass = GST_RTSP_AUTH_GET_CLASS (auth);

  /* we need a token to check */
  if (state->token == NULL) {
    if (klass->authenticate) {
      if (!klass->authenticate (auth, state))
        goto authenticate_failed;
    }
  }
  if (state->token == NULL)
    goto no_auth;

  return TRUE;

/* ERRORS */
authenticate_failed:
  {
    GST_DEBUG_OBJECT (auth, "authenticate failed");
    return FALSE;
  }
no_auth:
  {
    GST_DEBUG_OBJECT (auth, "no authorization token found");
    return FALSE;
  }
}

static void
send_response (GstRTSPAuth * auth, GstRTSPStatusCode code,
    GstRTSPClientState * state)
{
  gst_rtsp_message_init_response (state->response, code,
      gst_rtsp_status_as_text (code), state->request);

  if (code == GST_RTSP_STS_UNAUTHORIZED) {
    /* we only have Basic for now */
    gst_rtsp_message_add_header (state->response, GST_RTSP_HDR_WWW_AUTHENTICATE,
        "Basic realm=\"GStreamer RTSP Server\"");
  }
  gst_rtsp_client_send_message (state->client, state->session, state->response);
}

/* new connection */
static gboolean
check_connect (GstRTSPAuth * auth, GstRTSPClientState * state,
    const gchar * check)
{
  GstRTSPAuthPrivate *priv = auth->priv;

  if (priv->certificate) {
    GTlsConnection *tls;

    /* configure the connection */
    tls = gst_rtsp_connection_get_tls (state->conn, NULL);
    g_tls_connection_set_certificate (tls, priv->certificate);
  }
  return TRUE;
}

/* check url and methods */
static gboolean
check_url (GstRTSPAuth * auth, GstRTSPClientState * state, const gchar * check)
{
  GstRTSPAuthPrivate *priv = auth->priv;

  if ((state->method & priv->methods) != 0)
    if (!ensure_authenticated (auth, state))
      goto not_authenticated;

  return TRUE;

  /* ERRORS */
not_authenticated:
  {
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, state);
    return FALSE;
  }
}

/* check access to media factory */
static gboolean
check_factory (GstRTSPAuth * auth, GstRTSPClientState * state,
    const gchar * check)
{
  const gchar *role;
  GstRTSPPermissions *perms;

  if (!(role = gst_rtsp_token_get_string (state->token,
              GST_RTSP_MEDIA_FACTORY_ROLE)))
    goto no_media_role;
  if (!(perms = gst_rtsp_media_factory_get_permissions (state->factory)))
    goto no_permissions;

  if (g_str_equal (check, "auth.check.media.factory.access")) {
    if (!gst_rtsp_permissions_is_allowed (perms, role,
            GST_RTSP_MEDIA_FACTORY_PERM_ACCESS))
      goto no_access;
  } else if (g_str_equal (check, "auth.check.media.factory.construct")) {
    if (!gst_rtsp_permissions_is_allowed (perms, role,
            GST_RTSP_MEDIA_FACTORY_PERM_CONSTRUCT))
      goto no_construct;
  }
  return TRUE;

  /* ERRORS */
no_media_role:
  {
    GST_DEBUG_OBJECT (auth, "no media factory role found");
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, state);
    return FALSE;
  }
no_permissions:
  {
    GST_DEBUG_OBJECT (auth, "no permissions on media factory found");
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, state);
    return FALSE;
  }
no_access:
  {
    GST_DEBUG_OBJECT (auth, "no permissions to access media factory");
    send_response (auth, GST_RTSP_STS_NOT_FOUND, state);
    return FALSE;
  }
no_construct:
  {
    GST_DEBUG_OBJECT (auth, "no permissions to construct media factory");
    send_response (auth, GST_RTSP_STS_UNAUTHORIZED, state);
    return FALSE;
  }
}

static gboolean
default_check (GstRTSPAuth * auth, GstRTSPClientState * state,
    const gchar * check)
{
  gboolean res = FALSE;

  /* FIXME, use hastable or so */
  if (g_str_equal (check, GST_RTSP_AUTH_CHECK_CONNECT)) {
    res = check_connect (auth, state, check);
  } else if (g_str_equal (check, GST_RTSP_AUTH_CHECK_URL)) {
    res = check_url (auth, state, check);
  } else if (g_str_has_prefix (check, "auth.check.media.factory.")) {
    res = check_factory (auth, state, check);
  }
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
  GstRTSPClientState *state;
  GstRTSPAuth *auth;

  g_return_val_if_fail (check != NULL, FALSE);

  if (!(state = gst_rtsp_client_state_get_current ()))
    goto no_state;

  /* no auth, we don't need to check */
  if (!(auth = state->auth))
    return TRUE;

  klass = GST_RTSP_AUTH_GET_CLASS (auth);

  GST_DEBUG_OBJECT (auth, "check authorization '%s'", check);

  if (klass->check)
    result = klass->check (auth, state, check);

  return result;

  /* ERRORS */
no_state:
  {
    GST_ERROR ("no clientstate found");
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
 * Returns: the base64 encoding of the string @user:@pass. g_free()
 *    after usage.
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
