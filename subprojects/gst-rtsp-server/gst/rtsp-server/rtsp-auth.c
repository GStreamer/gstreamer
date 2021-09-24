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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-auth.h"

struct _GstRTSPAuthPrivate
{
  GMutex lock;

  /* the TLS certificate */
  GTlsCertificate *certificate;
  GTlsDatabase *database;
  GTlsAuthenticationMode mode;
  GHashTable *basic;            /* protected by lock */
  GHashTable *digest, *nonces;  /* protected by lock */
  guint64 last_nonce_check;
  GstRTSPToken *default_token;
  GstRTSPMethod methods;
  GstRTSPAuthMethod auth_methods;
  gchar *realm;
};

typedef struct
{
  GstRTSPToken *token;
  gchar *pass;
  gchar *md5_pass;
} GstRTSPDigestEntry;

typedef struct
{
  gchar *nonce;
  gchar *ip;
  guint64 timestamp;
  gpointer client;
} GstRTSPDigestNonce;

static void
gst_rtsp_digest_entry_free (GstRTSPDigestEntry * entry)
{
  gst_rtsp_token_unref (entry->token);
  g_free (entry->pass);
  g_free (entry->md5_pass);
  g_free (entry);
}

static void
gst_rtsp_digest_nonce_free (GstRTSPDigestNonce * nonce)
{
  g_free (nonce->nonce);
  g_free (nonce->ip);
  g_free (nonce);
}

enum
{
  PROP_0,
  PROP_LAST
};

enum
{
  SIGNAL_ACCEPT_CERTIFICATE,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

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
static void default_generate_authenticate_header (GstRTSPAuth * auth,
    GstRTSPContext * ctx);


G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPAuth, gst_rtsp_auth, G_TYPE_OBJECT);

static void
gst_rtsp_auth_class_init (GstRTSPAuthClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_auth_get_property;
  gobject_class->set_property = gst_rtsp_auth_set_property;
  gobject_class->finalize = gst_rtsp_auth_finalize;

  klass->authenticate = default_authenticate;
  klass->check = default_check;
  klass->generate_authenticate_header = default_generate_authenticate_header;

  GST_DEBUG_CATEGORY_INIT (rtsp_auth_debug, "rtspauth", 0, "GstRTSPAuth");

  /**
   * GstRTSPAuth::accept-certificate:
   * @auth: a #GstRTSPAuth
   * @connection: a #GTlsConnection
   * @peer_cert: the peer's #GTlsCertificate
   * @errors: the problems with @peer_cert.
   *
   * Emitted during the TLS handshake after the client certificate has
   * been received. See also gst_rtsp_auth_set_tls_authentication_mode().
   *
   * Returns: %TRUE to accept @peer_cert (which will also
   * immediately end the signal emission). %FALSE to allow the signal
   * emission to continue, which will cause the handshake to fail if
   * no one else overrides it.
   *
   * Since: 1.6
   */
  signals[SIGNAL_ACCEPT_CERTIFICATE] = g_signal_new ("accept-certificate",
      G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPAuthClass, accept_certificate),
      g_signal_accumulator_true_handled, NULL, NULL,
      G_TYPE_BOOLEAN, 3, G_TYPE_TLS_CONNECTION, G_TYPE_TLS_CERTIFICATE,
      G_TYPE_TLS_CERTIFICATE_FLAGS);
}

static void
gst_rtsp_auth_init (GstRTSPAuth * auth)
{
  GstRTSPAuthPrivate *priv;

  auth->priv = priv = gst_rtsp_auth_get_instance_private (auth);

  g_mutex_init (&priv->lock);

  priv->basic = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) gst_rtsp_token_unref);
  priv->digest = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) gst_rtsp_digest_entry_free);
  priv->nonces = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) gst_rtsp_digest_nonce_free);

  /* bitwise or of all methods that need authentication */
  priv->methods = 0;
  priv->auth_methods = GST_RTSP_AUTH_BASIC;
  priv->realm = g_strdup ("GStreamer RTSP Server");
}

static void
gst_rtsp_auth_finalize (GObject * obj)
{
  GstRTSPAuth *auth = GST_RTSP_AUTH (obj);
  GstRTSPAuthPrivate *priv = auth->priv;

  GST_INFO ("finalize auth %p", auth);

  if (priv->certificate)
    g_object_unref (priv->certificate);
  if (priv->database)
    g_object_unref (priv->database);
  g_hash_table_unref (priv->basic);
  g_hash_table_unref (priv->digest);
  g_hash_table_unref (priv->nonces);
  if (priv->default_token)
    gst_rtsp_token_unref (priv->default_token);
  g_mutex_clear (&priv->lock);
  g_free (priv->realm);

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
 * Returns: (transfer full) (nullable): the #GTlsCertificate of @auth. g_object_unref() after
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
 * gst_rtsp_auth_set_tls_database:
 * @auth: a #GstRTSPAuth
 * @database: (transfer none) (allow-none): a #GTlsDatabase
 *
 * Sets the certificate database that is used to verify peer certificates.
 * If set to %NULL (the default), then peer certificate validation will always
 * set the %G_TLS_CERTIFICATE_UNKNOWN_CA error.
 *
 * Since: 1.6
 */
void
gst_rtsp_auth_set_tls_database (GstRTSPAuth * auth, GTlsDatabase * database)
{
  GstRTSPAuthPrivate *priv;
  GTlsDatabase *old;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));

  priv = auth->priv;

  if (database)
    g_object_ref (database);

  g_mutex_lock (&priv->lock);
  old = priv->database;
  priv->database = database;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_auth_get_tls_database:
 * @auth: a #GstRTSPAuth
 *
 * Get the #GTlsDatabase used for verifying client certificate.
 *
 * Returns: (transfer full) (nullable): the #GTlsDatabase of @auth. g_object_unref() after
 * usage.
 * Since: 1.6
 */
GTlsDatabase *
gst_rtsp_auth_get_tls_database (GstRTSPAuth * auth)
{
  GstRTSPAuthPrivate *priv;
  GTlsDatabase *result;

  g_return_val_if_fail (GST_IS_RTSP_AUTH (auth), NULL);

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->database))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_auth_set_tls_authentication_mode:
 * @auth: a #GstRTSPAuth
 * @mode: a #GTlsAuthenticationMode
 *
 * The #GTlsAuthenticationMode to set on the underlying GTlsServerConnection.
 * When set to another value than %G_TLS_AUTHENTICATION_NONE,
 * #GstRTSPAuth::accept-certificate signal will be emitted and must be handled.
 *
 * Since: 1.6
 */
void
gst_rtsp_auth_set_tls_authentication_mode (GstRTSPAuth * auth,
    GTlsAuthenticationMode mode)
{
  GstRTSPAuthPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  priv->mode = mode;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_auth_get_tls_authentication_mode:
 * @auth: a #GstRTSPAuth
 *
 * Get the #GTlsAuthenticationMode.
 *
 * Returns: (transfer full): the #GTlsAuthenticationMode.
 */
GTlsAuthenticationMode
gst_rtsp_auth_get_tls_authentication_mode (GstRTSPAuth * auth)
{
  GstRTSPAuthPrivate *priv;
  GTlsAuthenticationMode result;

  g_return_val_if_fail (GST_IS_RTSP_AUTH (auth), G_TLS_AUTHENTICATION_NONE);

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  result = priv->mode;
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
 * Returns: (transfer full) (nullable): the #GstRTSPToken of @auth. gst_rtsp_token_unref() after
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
 * Removes @basic authentication token.
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

/**
 * gst_rtsp_auth_add_digest:
 * @auth: a #GstRTSPAuth
 * @user: the digest user name
 * @pass: the digest password
 * @token: (transfer none): authorisation token
 *
 * Add a digest @user and @pass for the default authentication algorithm that
 * enables the client with privileges listed in @token.
 *
 * Since: 1.12
 */
void
gst_rtsp_auth_add_digest (GstRTSPAuth * auth, const gchar * user,
    const gchar * pass, GstRTSPToken * token)
{
  GstRTSPAuthPrivate *priv;
  GstRTSPDigestEntry *entry;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));
  g_return_if_fail (user != NULL);
  g_return_if_fail (pass != NULL);
  g_return_if_fail (GST_IS_RTSP_TOKEN (token));

  priv = auth->priv;

  entry = g_new0 (GstRTSPDigestEntry, 1);
  entry->token = gst_rtsp_token_ref (token);
  entry->pass = g_strdup (pass);

  g_mutex_lock (&priv->lock);
  g_hash_table_replace (priv->digest, g_strdup (user), entry);
  g_mutex_unlock (&priv->lock);
}

/* With auth lock taken */
static gboolean
update_digest_cb (gchar * key, GstRTSPDigestEntry * entry, GHashTable * digest)
{
  g_hash_table_replace (digest, key, entry);

  return TRUE;
}

/**
 * gst_rtsp_auth_parse_htdigest:
 * @path: (type filename): Path to the htdigest file
 * @token: (transfer none): authorisation token
 *
 * Parse the contents of the file at @path and enable the privileges
 * listed in @token for the users it describes.
 *
 * The format of the file is expected to match the format described by
 * <https://en.wikipedia.org/wiki/Digest_access_authentication#The_.htdigest_file>,
 * as output by the `htdigest` command.
 *
 * Returns: %TRUE if the file was successfully parsed, %FALSE otherwise.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_auth_parse_htdigest (GstRTSPAuth * auth, const gchar * path,
    GstRTSPToken * token)
{
  GstRTSPAuthPrivate *priv;
  gboolean ret = FALSE;
  gchar *line = NULL;
  gchar *eol = NULL;
  gchar *contents = NULL;
  GError *error = NULL;
  GHashTable *new_entries =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) gst_rtsp_digest_entry_free);


  g_return_val_if_fail (GST_IS_RTSP_AUTH (auth), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (GST_IS_RTSP_TOKEN (token), FALSE);

  priv = auth->priv;
  if (!g_file_get_contents (path, &contents, NULL, &error)) {
    GST_ERROR_OBJECT (auth, "Could not parse htdigest: %s", error->message);
    goto done;
  }

  for (line = contents; line && *line; line = eol ? eol + 1 : NULL) {
    GstRTSPDigestEntry *entry;
    gchar **strv;
    eol = strchr (line, '\n');

    if (eol)
      *eol = '\0';

    strv = g_strsplit (line, ":", -1);

    if (!(strv[0] && strv[1] && strv[2] && !strv[3])) {
      GST_ERROR_OBJECT (auth, "Invalid htdigest format");
      g_strfreev (strv);
      goto done;
    }

    if (strlen (strv[2]) != 32) {
      GST_ERROR_OBJECT (auth,
          "Invalid htdigest format, hash is expected to be 32 characters long");
      g_strfreev (strv);
      goto done;
    }

    entry = g_new0 (GstRTSPDigestEntry, 1);
    entry->token = gst_rtsp_token_ref (token);
    entry->md5_pass = g_strdup (strv[2]);
    g_hash_table_replace (new_entries, g_strdup (strv[0]), entry);
    g_strfreev (strv);
  }

  ret = TRUE;

  /* We only update digest if the file was entirely valid */
  g_mutex_lock (&priv->lock);
  g_hash_table_foreach_steal (new_entries, (GHRFunc) update_digest_cb,
      priv->digest);
  g_mutex_unlock (&priv->lock);

done:
  if (error)
    g_clear_error (&error);
  g_free (contents);
  g_hash_table_unref (new_entries);
  return ret;
}

/**
 * gst_rtsp_auth_remove_digest:
 * @auth: a #GstRTSPAuth
 * @user: (transfer none): the digest user name
 *
 * Removes a digest user.
 *
 * Since: 1.12
 */
void
gst_rtsp_auth_remove_digest (GstRTSPAuth * auth, const gchar * user)
{
  GstRTSPAuthPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));
  g_return_if_fail (user != NULL);

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  g_hash_table_remove (priv->digest, user);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_auth_set_supported_methods:
 * @auth: a #GstRTSPAuth
 * @methods: supported methods
 *
 * Sets the supported authentication @methods for @auth.
 *
 * Since: 1.12
 */
void
gst_rtsp_auth_set_supported_methods (GstRTSPAuth * auth,
    GstRTSPAuthMethod methods)
{
  GstRTSPAuthPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_AUTH (auth));

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  priv->auth_methods = methods;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_auth_get_supported_methods:
 * @auth: a #GstRTSPAuth
 *
 * Gets the supported authentication methods of @auth.
 *
 * Returns: The supported authentication methods
 *
 * Since: 1.12
 */
GstRTSPAuthMethod
gst_rtsp_auth_get_supported_methods (GstRTSPAuth * auth)
{
  GstRTSPAuthPrivate *priv;
  GstRTSPAuthMethod methods;

  g_return_val_if_fail (GST_IS_RTSP_AUTH (auth), 0);

  priv = auth->priv;

  g_mutex_lock (&priv->lock);
  methods = priv->auth_methods;
  g_mutex_unlock (&priv->lock);

  return methods;
}

typedef struct
{
  GstRTSPAuth *auth;
  GstRTSPDigestNonce *nonce;
} RemoveNonceData;

static void
remove_nonce (gpointer data, GObject * object)
{
  RemoveNonceData *remove_nonce_data = data;

  g_mutex_lock (&remove_nonce_data->auth->priv->lock);
  g_hash_table_remove (remove_nonce_data->auth->priv->nonces,
      remove_nonce_data->nonce->nonce);
  g_mutex_unlock (&remove_nonce_data->auth->priv->lock);

  g_object_unref (remove_nonce_data->auth);
  g_free (remove_nonce_data);
}

static gboolean
default_digest_auth (GstRTSPAuth * auth, GstRTSPContext * ctx,
    GstRTSPAuthParam ** param)
{
  const gchar *realm = NULL, *user = NULL, *nonce = NULL;
  const gchar *response = NULL, *uri = NULL;
  GstRTSPDigestNonce *nonce_entry = NULL;
  GstRTSPDigestEntry *digest_entry;
  gchar *expected_response = NULL;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (auth, "check Digest auth");

  if (!param)
    return ret;

  while (*param) {
    if (!realm && strcmp ((*param)->name, "realm") == 0 && (*param)->value)
      realm = (*param)->value;
    else if (!user && strcmp ((*param)->name, "username") == 0
        && (*param)->value)
      user = (*param)->value;
    else if (!nonce && strcmp ((*param)->name, "nonce") == 0 && (*param)->value)
      nonce = (*param)->value;
    else if (!response && strcmp ((*param)->name, "response") == 0
        && (*param)->value)
      response = (*param)->value;
    else if (!uri && strcmp ((*param)->name, "uri") == 0 && (*param)->value)
      uri = (*param)->value;

    param++;
  }

  if (!realm || !user || !nonce || !response || !uri)
    return FALSE;

  g_mutex_lock (&auth->priv->lock);
  digest_entry = g_hash_table_lookup (auth->priv->digest, user);
  if (!digest_entry)
    goto out;
  nonce_entry = g_hash_table_lookup (auth->priv->nonces, nonce);
  if (!nonce_entry)
    goto out;

  if (strcmp (nonce_entry->ip, gst_rtsp_connection_get_ip (ctx->conn)) != 0)
    goto out;
  if (nonce_entry->client && nonce_entry->client != ctx->client)
    goto out;

  if (digest_entry->md5_pass) {
    expected_response = gst_rtsp_generate_digest_auth_response_from_md5 (NULL,
        gst_rtsp_method_as_text (ctx->method), digest_entry->md5_pass,
        uri, nonce);
  } else {
    expected_response =
        gst_rtsp_generate_digest_auth_response (NULL,
        gst_rtsp_method_as_text (ctx->method), realm, user,
        digest_entry->pass, uri, nonce);
  }

  if (!expected_response || strcmp (response, expected_response) != 0)
    goto out;

  ctx->token = digest_entry->token;
  ret = TRUE;

out:
  if (nonce_entry && !nonce_entry->client) {
    RemoveNonceData *remove_nonce_data = g_new (RemoveNonceData, 1);

    nonce_entry->client = ctx->client;
    remove_nonce_data->nonce = nonce_entry;
    remove_nonce_data->auth = g_object_ref (auth);
    g_object_weak_ref (G_OBJECT (ctx->client), remove_nonce, remove_nonce_data);
  }
  g_mutex_unlock (&auth->priv->lock);

  g_free (expected_response);

  return ret;
}

static gboolean
default_authenticate (GstRTSPAuth * auth, GstRTSPContext * ctx)
{
  GstRTSPAuthPrivate *priv = auth->priv;
  GstRTSPAuthCredential **credentials, **credential;

  GST_DEBUG_OBJECT (auth, "authenticate");

  g_mutex_lock (&priv->lock);
  /* FIXME, need to ref but we have no way to unref when the ctx is
   * popped */
  ctx->token = priv->default_token;
  g_mutex_unlock (&priv->lock);

  credentials =
      gst_rtsp_message_parse_auth_credentials (ctx->request,
      GST_RTSP_HDR_AUTHORIZATION);
  if (!credentials)
    goto no_auth;

  /* parse type */
  credential = credentials;
  while (*credential) {
    if ((*credential)->scheme == GST_RTSP_AUTH_BASIC) {
      GstRTSPToken *token;

      GST_DEBUG_OBJECT (auth, "check Basic auth");
      g_mutex_lock (&priv->lock);
      if ((*credential)->authorization && (token =
              g_hash_table_lookup (priv->basic,
                  (*credential)->authorization))) {
        GST_DEBUG_OBJECT (auth, "setting token %p", token);
        ctx->token = token;
        g_mutex_unlock (&priv->lock);
        break;
      }
      g_mutex_unlock (&priv->lock);
    } else if ((*credential)->scheme == GST_RTSP_AUTH_DIGEST) {
      if (default_digest_auth (auth, ctx, (*credential)->params))
        break;
    }

    credential++;
  }

  gst_rtsp_auth_credentials_free (credentials);
  return TRUE;

no_auth:
  {
    GST_DEBUG_OBJECT (auth, "no authorization header found");
    return TRUE;
  }
}

static void
default_generate_authenticate_header (GstRTSPAuth * auth, GstRTSPContext * ctx)
{
  if (auth->priv->auth_methods & GST_RTSP_AUTH_BASIC) {
    gchar *auth_header =
        g_strdup_printf ("Basic realm=\"%s\"", auth->priv->realm);
    gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_WWW_AUTHENTICATE,
        auth_header);
    g_free (auth_header);
  }

  if (auth->priv->auth_methods & GST_RTSP_AUTH_DIGEST) {
    GstRTSPDigestNonce *nonce;
    gchar *nonce_value, *auth_header;

    nonce_value =
        g_strdup_printf ("%08x%08x", g_random_int (), g_random_int ());

    auth_header =
        g_strdup_printf
        ("Digest realm=\"%s\", nonce=\"%s\"", auth->priv->realm, nonce_value);
    gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_WWW_AUTHENTICATE,
        auth_header);
    g_free (auth_header);

    nonce = g_new0 (GstRTSPDigestNonce, 1);
    nonce->nonce = g_strdup (nonce_value);
    nonce->timestamp = g_get_monotonic_time ();
    nonce->ip = g_strdup (gst_rtsp_connection_get_ip (ctx->conn));
    g_mutex_lock (&auth->priv->lock);
    g_hash_table_replace (auth->priv->nonces, nonce_value, nonce);

    if (auth->priv->last_nonce_check == 0)
      auth->priv->last_nonce_check = nonce->timestamp;

    /* 30 second nonce timeout */
    if (nonce->timestamp - auth->priv->last_nonce_check >= 30 * G_USEC_PER_SEC) {
      GHashTableIter iter;
      gpointer key, value;

      g_hash_table_iter_init (&iter, auth->priv->nonces);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        GstRTSPDigestNonce *tmp = value;

        if (!tmp->client
            && nonce->timestamp - tmp->timestamp >= 30 * G_USEC_PER_SEC)
          g_hash_table_iter_remove (&iter);
      }
      auth->priv->last_nonce_check = nonce->timestamp;
    }

    g_mutex_unlock (&auth->priv->lock);
  }
}

static void
send_response (GstRTSPAuth * auth, GstRTSPStatusCode code, GstRTSPContext * ctx)
{
  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  if (code == GST_RTSP_STS_UNAUTHORIZED) {
    GstRTSPAuthClass *klass;

    klass = GST_RTSP_AUTH_GET_CLASS (auth);

    if (klass->generate_authenticate_header)
      klass->generate_authenticate_header (auth, ctx);
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

static gboolean
accept_certificate_cb (GTlsConnection * conn, GTlsCertificate * peer_cert,
    GTlsCertificateFlags errors, GstRTSPAuth * auth)
{
  gboolean ret = FALSE;

  g_signal_emit (auth, signals[SIGNAL_ACCEPT_CERTIFICATE], 0,
      conn, peer_cert, errors, &ret);

  return ret;
}

/* new connection */
static gboolean
check_connect (GstRTSPAuth * auth, GstRTSPContext * ctx, const gchar * check)
{
  GstRTSPAuthPrivate *priv = auth->priv;
  GTlsConnection *tls;

  /* configure the connection */

  if (priv->certificate) {
    tls = gst_rtsp_connection_get_tls (ctx->conn, NULL);
    g_tls_connection_set_certificate (tls, priv->certificate);
  }

  if (priv->mode != G_TLS_AUTHENTICATION_NONE) {
    tls = gst_rtsp_connection_get_tls (ctx->conn, NULL);
    g_tls_connection_set_database (tls, priv->database);
    g_object_set (tls, "authentication-mode", priv->mode, NULL);
    g_signal_connect (tls, "accept-certificate",
        G_CALLBACK (accept_certificate_cb), auth);
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

/**
 * gst_rtsp_auth_set_realm:
 *
 * Set the @realm of @auth
 *
 * Since: 1.16
 */
void
gst_rtsp_auth_set_realm (GstRTSPAuth * auth, const gchar * realm)
{
  g_return_if_fail (GST_IS_RTSP_AUTH (auth));
  g_return_if_fail (realm != NULL);

  if (auth->priv->realm)
    g_free (auth->priv->realm);

  auth->priv->realm = g_strdup (realm);
}

/**
 * gst_rtsp_auth_get_realm:
 *
 * Returns: (transfer full): the @realm of @auth
 *
 * Since: 1.16
 */
gchar *
gst_rtsp_auth_get_realm (GstRTSPAuth * auth)
{
  g_return_val_if_fail (GST_IS_RTSP_AUTH (auth), NULL);

  return g_strdup (auth->priv->realm);
}
