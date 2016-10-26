/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstdtlsconnection.h"

#include "gstdtlsagent.h"
#include "gstdtlscertificate.h"

#ifdef __APPLE__
# define __AVAILABILITYMACROS__
# define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_dtls_connection_debug);
#define GST_CAT_DEFAULT gst_dtls_connection_debug
G_DEFINE_TYPE_WITH_CODE (GstDtlsConnection, gst_dtls_connection, G_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_dtls_connection_debug, "dtlsconnection", 0,
        "DTLS Connection"));

#define GST_DTLS_CONNECTION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), GST_TYPE_DTLS_CONNECTION, GstDtlsConnectionPrivate))

#define SRTP_KEY_LEN 16
#define SRTP_SALT_LEN 14

enum
{
  SIGNAL_ON_ENCODER_KEY,
  SIGNAL_ON_DECODER_KEY,
  SIGNAL_ON_PEER_CERTIFICATE,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

enum
{
  PROP_0,
  PROP_AGENT,
  NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

static int connection_ex_index;

static GstClock *system_clock;
static void handle_timeout (gpointer data, gpointer user_data);

struct _GstDtlsConnectionPrivate
{
  SSL *ssl;
  BIO *bio;

  gboolean is_client;
  gboolean is_alive;
  gboolean keys_exported;

  GMutex mutex;
  GCond condition;
  gpointer bio_buffer;
  gint bio_buffer_len;
  gint bio_buffer_offset;

  GClosure *send_closure;

  gboolean timeout_pending;
  GThreadPool *thread_pool;
};

static void gst_dtls_connection_finalize (GObject * gobject);
static void gst_dtls_connection_set_property (GObject *, guint prop_id,
    const GValue *, GParamSpec *);

static void log_state (GstDtlsConnection *, const gchar * str);
static void export_srtp_keys (GstDtlsConnection *);
static void openssl_poll (GstDtlsConnection *);
static int openssl_verify_callback (int preverify_ok,
    X509_STORE_CTX * x509_ctx);

static BIO_METHOD *BIO_s_gst_dtls_connection (void);
static int bio_method_write (BIO *, const char *data, int size);
static int bio_method_read (BIO *, char *out_buffer, int size);
static long bio_method_ctrl (BIO *, int cmd, long arg1, void *arg2);
static int bio_method_new (BIO *);
static int bio_method_free (BIO *);

static void
gst_dtls_connection_class_init (GstDtlsConnectionClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstDtlsConnectionPrivate));

  gobject_class->set_property = gst_dtls_connection_set_property;

  connection_ex_index =
      SSL_get_ex_new_index (0, (gpointer) "gstdtlsagent connection index", NULL,
      NULL, NULL);

  signals[SIGNAL_ON_DECODER_KEY] =
      g_signal_new ("on-decoder-key", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 3,
      G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT);

  signals[SIGNAL_ON_ENCODER_KEY] =
      g_signal_new ("on-encoder-key", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 3,
      G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT);

  signals[SIGNAL_ON_PEER_CERTIFICATE] =
      g_signal_new ("on-peer-certificate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);

  properties[PROP_AGENT] =
      g_param_spec_object ("agent",
      "DTLS Agent",
      "Agent to use in creation of the connection",
      GST_TYPE_DTLS_AGENT,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, NUM_PROPERTIES, properties);

  _gst_dtls_init_openssl ();

  gobject_class->finalize = gst_dtls_connection_finalize;

  system_clock = gst_system_clock_obtain ();
}

static void
gst_dtls_connection_init (GstDtlsConnection * self)
{
  GstDtlsConnectionPrivate *priv = GST_DTLS_CONNECTION_GET_PRIVATE (self);
  self->priv = priv;

  priv->ssl = NULL;
  priv->bio = NULL;

  priv->send_closure = NULL;

  priv->is_client = FALSE;
  priv->is_alive = TRUE;
  priv->keys_exported = FALSE;

  priv->bio_buffer = NULL;
  priv->bio_buffer_len = 0;
  priv->bio_buffer_offset = 0;

  g_mutex_init (&priv->mutex);
  g_cond_init (&priv->condition);

  /* Thread pool for handling timeouts, we only need one thread for that
   * really and share threads with all other thread pools around there as
   * this is not going to happen very often */
  priv->thread_pool = g_thread_pool_new (handle_timeout, self, 1, FALSE, NULL);
  g_assert (priv->thread_pool);
  priv->timeout_pending = FALSE;
}

static void
gst_dtls_connection_finalize (GObject * gobject)
{
  GstDtlsConnection *self = GST_DTLS_CONNECTION (gobject);
  GstDtlsConnectionPrivate *priv = self->priv;

  g_thread_pool_free (priv->thread_pool, TRUE, TRUE);
  priv->thread_pool = NULL;

  SSL_free (priv->ssl);
  priv->ssl = NULL;

  if (priv->send_closure) {
    g_closure_unref (priv->send_closure);
    priv->send_closure = NULL;
  }

  g_mutex_clear (&priv->mutex);
  g_cond_clear (&priv->condition);

  GST_DEBUG_OBJECT (self, "finalized");

  G_OBJECT_CLASS (gst_dtls_connection_parent_class)->finalize (gobject);
}

#if OPENSSL_VERSION_NUMBER < 0x10100001L
static void
BIO_set_data (BIO * bio, void *ptr)
{
  bio->ptr = ptr;
}

static void *
BIO_get_data (BIO * bio)
{
  return bio->ptr;
}

static void
BIO_set_shutdown (BIO * bio, int shutdown)
{
  bio->shutdown = shutdown;
}

static void
BIO_set_init (BIO * bio, int init)
{
  bio->init = init;
}

static X509 *
X509_STORE_CTX_get0_cert (X509_STORE_CTX * ctx)
{
  return ctx->cert;
}
#endif

static void
gst_dtls_connection_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDtlsConnection *self = GST_DTLS_CONNECTION (object);
  GstDtlsAgent *agent;
  GstDtlsConnectionPrivate *priv = self->priv;
  SSL_CTX *ssl_context;

  switch (prop_id) {
    case PROP_AGENT:
      g_return_if_fail (!priv->ssl);
      agent = GST_DTLS_AGENT (g_value_get_object (value));
      g_return_if_fail (GST_IS_DTLS_AGENT (agent));

      ssl_context = _gst_dtls_agent_peek_context (agent);

      priv->ssl = SSL_new (ssl_context);
      g_return_if_fail (priv->ssl);

      priv->bio = BIO_new (BIO_s_gst_dtls_connection ());
      g_return_if_fail (priv->bio);

      BIO_set_data (priv->bio, self);
      SSL_set_bio (priv->ssl, priv->bio, priv->bio);

      SSL_set_verify (priv->ssl,
          SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
          openssl_verify_callback);
      SSL_set_ex_data (priv->ssl, connection_ex_index, self);

      log_state (self, "connection created");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
  }
}

void
gst_dtls_connection_start (GstDtlsConnection * self, gboolean is_client)
{
  GstDtlsConnectionPrivate *priv;

  priv = self->priv;

  g_return_if_fail (priv->send_closure);
  g_return_if_fail (priv->ssl);
  g_return_if_fail (priv->bio);

  GST_TRACE_OBJECT (self, "locking @ start");
  g_mutex_lock (&priv->mutex);
  GST_TRACE_OBJECT (self, "locked @ start");

  priv->is_alive = TRUE;
  priv->bio_buffer = NULL;
  priv->bio_buffer_len = 0;
  priv->bio_buffer_offset = 0;
  priv->keys_exported = FALSE;

  priv->is_client = is_client;
  if (priv->is_client) {
    SSL_set_connect_state (priv->ssl);
  } else {
    SSL_set_accept_state (priv->ssl);
  }
  log_state (self, "initial state set");

  openssl_poll (self);

  log_state (self, "first poll done");

  GST_TRACE_OBJECT (self, "unlocking @ start");
  g_mutex_unlock (&priv->mutex);
}

static void
handle_timeout (gpointer data, gpointer user_data)
{
  GstDtlsConnection *self = user_data;
  GstDtlsConnectionPrivate *priv;
  gint ret;

  priv = self->priv;

  g_mutex_lock (&priv->mutex);
  priv->timeout_pending = FALSE;
  if (priv->is_alive) {
    ret = DTLSv1_handle_timeout (priv->ssl);

    GST_DEBUG_OBJECT (self, "handle timeout returned %d, is_alive: %d", ret,
        priv->is_alive);

    if (ret < 0) {
      GST_WARNING_OBJECT (self, "handling timeout failed");
    } else if (ret > 0) {
      log_state (self, "handling timeout before poll");
      openssl_poll (self);
      log_state (self, "handling timeout after poll");
    }
  }
  g_mutex_unlock (&priv->mutex);
}

static gboolean
schedule_timeout_handling (GstClock * clock, GstClockTime time, GstClockID id,
    gpointer user_data)
{
  GstDtlsConnection *self = user_data;

  g_mutex_lock (&self->priv->mutex);
  if (self->priv->is_alive && !self->priv->timeout_pending) {
    self->priv->timeout_pending = TRUE;

    GST_TRACE_OBJECT (self, "Schedule timeout now");
    g_thread_pool_push (self->priv->thread_pool, GINT_TO_POINTER (0xc0ffee),
        NULL);
  }
  g_mutex_unlock (&self->priv->mutex);

  return TRUE;
}

static void
gst_dtls_connection_check_timeout_locked (GstDtlsConnection * self)
{
  GstDtlsConnectionPrivate *priv;
  struct timeval timeout;
  gint64 end_time, wait_time;

  g_return_if_fail (GST_IS_DTLS_CONNECTION (self));

  priv = self->priv;

  if (DTLSv1_get_timeout (priv->ssl, &timeout)) {
    wait_time = timeout.tv_sec * G_USEC_PER_SEC + timeout.tv_usec;

    GST_DEBUG_OBJECT (self, "waiting for %" G_GINT64_FORMAT " usec", wait_time);
    if (wait_time) {
      GstClockID clock_id;
#ifndef G_DISABLE_ASSERT
      GstClockReturn clock_return;
#endif

      end_time = gst_clock_get_time (system_clock) + wait_time * GST_USECOND;

      clock_id = gst_clock_new_single_shot_id (system_clock, end_time);
#ifndef G_DISABLE_ASSERT
      clock_return =
#else
      (void)
#endif
          gst_clock_id_wait_async (clock_id, schedule_timeout_handling,
          g_object_ref (self), (GDestroyNotify) g_object_unref);
      g_assert (clock_return == GST_CLOCK_OK);
      gst_clock_id_unref (clock_id);
    } else {
      if (self->priv->is_alive && !self->priv->timeout_pending) {
        self->priv->timeout_pending = TRUE;
        GST_TRACE_OBJECT (self, "Schedule timeout now");

        g_thread_pool_push (self->priv->thread_pool, GINT_TO_POINTER (0xc0ffee),
            NULL);
      }
    }
  } else {
    GST_DEBUG_OBJECT (self, "no timeout set");
  }
}

void
gst_dtls_connection_check_timeout (GstDtlsConnection * self)
{
  GstDtlsConnectionPrivate *priv;

  g_return_if_fail (GST_IS_DTLS_CONNECTION (self));

  priv = self->priv;

  GST_TRACE_OBJECT (self, "locking @ start_timeout");
  g_mutex_lock (&priv->mutex);
  GST_TRACE_OBJECT (self, "locked @ start_timeout");
  gst_dtls_connection_check_timeout_locked (self);
  g_mutex_unlock (&priv->mutex);
  GST_TRACE_OBJECT (self, "unlocking @ start_timeout");
}

void
gst_dtls_connection_stop (GstDtlsConnection * self)
{
  g_return_if_fail (GST_IS_DTLS_CONNECTION (self));
  g_return_if_fail (self->priv->ssl);
  g_return_if_fail (self->priv->bio);

  GST_DEBUG_OBJECT (self, "stopping connection");

  GST_TRACE_OBJECT (self, "locking @ stop");
  g_mutex_lock (&self->priv->mutex);
  GST_TRACE_OBJECT (self, "locked @ stop");

  self->priv->is_alive = FALSE;
  GST_TRACE_OBJECT (self, "signaling @ stop");
  g_cond_signal (&self->priv->condition);
  GST_TRACE_OBJECT (self, "signaled @ stop");

  GST_TRACE_OBJECT (self, "unlocking @ stop");
  g_mutex_unlock (&self->priv->mutex);

  GST_DEBUG_OBJECT (self, "stopped connection");
}

void
gst_dtls_connection_close (GstDtlsConnection * self)
{
  g_return_if_fail (GST_IS_DTLS_CONNECTION (self));
  g_return_if_fail (self->priv->ssl);
  g_return_if_fail (self->priv->bio);

  GST_DEBUG_OBJECT (self, "closing connection");

  GST_TRACE_OBJECT (self, "locking @ close");
  g_mutex_lock (&self->priv->mutex);
  GST_TRACE_OBJECT (self, "locked @ close");

  if (self->priv->is_alive) {
    self->priv->is_alive = FALSE;
    g_cond_signal (&self->priv->condition);
  }

  GST_TRACE_OBJECT (self, "unlocking @ close");
  g_mutex_unlock (&self->priv->mutex);

  GST_DEBUG_OBJECT (self, "closed connection");
}

void
gst_dtls_connection_set_send_callback (GstDtlsConnection * self,
    GClosure * closure)
{
  g_return_if_fail (GST_IS_DTLS_CONNECTION (self));

  GST_TRACE_OBJECT (self, "locking @ set_send_callback");
  g_mutex_lock (&self->priv->mutex);
  GST_TRACE_OBJECT (self, "locked @ set_send_callback");

  if (self->priv->send_closure) {
    g_closure_unref (self->priv->send_closure);
    self->priv->send_closure = NULL;
  }
  self->priv->send_closure = closure;

  if (closure && G_CLOSURE_NEEDS_MARSHAL (closure)) {
    g_closure_set_marshal (closure, g_cclosure_marshal_generic);
  }

  GST_TRACE_OBJECT (self, "unlocking @ set_send_callback");
  g_mutex_unlock (&self->priv->mutex);
}

gint
gst_dtls_connection_process (GstDtlsConnection * self, gpointer data, gint len)
{
  GstDtlsConnectionPrivate *priv;
  gint result;

  g_return_val_if_fail (GST_IS_DTLS_CONNECTION (self), 0);
  g_return_val_if_fail (self->priv->ssl, 0);
  g_return_val_if_fail (self->priv->bio, 0);

  priv = self->priv;

  GST_TRACE_OBJECT (self, "locking @ process");
  g_mutex_lock (&priv->mutex);
  GST_TRACE_OBJECT (self, "locked @ process");

  g_warn_if_fail (!priv->bio_buffer);

  priv->bio_buffer = data;
  priv->bio_buffer_len = len;
  priv->bio_buffer_offset = 0;

  log_state (self, "process start");

  if (SSL_want_write (priv->ssl)) {
    openssl_poll (self);
    log_state (self, "process want write, after poll");
  }

  result = SSL_read (priv->ssl, data, len);

  log_state (self, "process after read");

  openssl_poll (self);

  log_state (self, "process after poll");

  GST_DEBUG_OBJECT (self, "read result: %d", result);

  GST_TRACE_OBJECT (self, "unlocking @ process");
  g_mutex_unlock (&priv->mutex);

  return result;
}

gint
gst_dtls_connection_send (GstDtlsConnection * self, gpointer data, gint len)
{
  int ret = 0;

  g_return_val_if_fail (GST_IS_DTLS_CONNECTION (self), 0);

  g_return_val_if_fail (self->priv->ssl, 0);
  g_return_val_if_fail (self->priv->bio, 0);

  GST_TRACE_OBJECT (self, "locking @ send");
  g_mutex_lock (&self->priv->mutex);
  GST_TRACE_OBJECT (self, "locked @ send");

  if (SSL_is_init_finished (self->priv->ssl)) {
    ret = SSL_write (self->priv->ssl, data, len);
    GST_DEBUG_OBJECT (self, "data sent: input was %d B, output is %d B", len,
        ret);
  } else {
    GST_WARNING_OBJECT (self,
        "tried to send data before handshake was complete");
    ret = 0;
  }

  GST_TRACE_OBJECT (self, "unlocking @ send");
  g_mutex_unlock (&self->priv->mutex);

  return ret;
}

/*
     ######   #######  ##    ##
    ##    ## ##     ## ###   ##
    ##       ##     ## ####  ##
    ##       ##     ## ## ## ##
    ##       ##     ## ##  ####
    ##    ## ##     ## ##   ###
     ######   #######  ##    ##
*/

static void
log_state (GstDtlsConnection * self, const gchar * str)
{
  GstDtlsConnectionPrivate *priv = self->priv;
  guint states = 0;

  states |= (! !SSL_is_init_finished (priv->ssl) << 0);
  states |= (! !SSL_in_init (priv->ssl) << 4);
  states |= (! !SSL_in_before (priv->ssl) << 8);
  states |= (! !SSL_in_connect_init (priv->ssl) << 12);
  states |= (! !SSL_in_accept_init (priv->ssl) << 16);
  states |= (! !SSL_want_write (priv->ssl) << 20);
  states |= (! !SSL_want_read (priv->ssl) << 24);

#if OPENSSL_VERSION_NUMBER < 0x10100001L
  GST_LOG_OBJECT (self, "%s: role=%s buf=(%d,%p:%d/%d) %x|%x %s",
      str,
      priv->is_client ? "client" : "server",
      pqueue_size (priv->ssl->d1->sent_messages),
      priv->bio_buffer,
      priv->bio_buffer_offset,
      priv->bio_buffer_len,
      states, SSL_get_state (priv->ssl), SSL_state_string_long (priv->ssl));
#else
  GST_LOG_OBJECT (self, "%s: role=%s buf=(%p:%d/%d) %x|%x %s",
      str,
      priv->is_client ? "client" : "server",
      priv->bio_buffer,
      priv->bio_buffer_offset,
      priv->bio_buffer_len,
      states, SSL_get_state (priv->ssl), SSL_state_string_long (priv->ssl));
#endif
}

static void
export_srtp_keys (GstDtlsConnection * self)
{
  typedef struct
  {
    guint8 v[SRTP_KEY_LEN];
  } Key;

  typedef struct
  {
    guint8 v[SRTP_SALT_LEN];
  } Salt;

  struct
  {
    Key client_key;
    Key server_key;
    Salt client_salt;
    Salt server_salt;
  } exported_keys;

  struct
  {
    Key key;
    Salt salt;
  } client_key, server_key;

  SRTP_PROTECTION_PROFILE *profile;
  GstDtlsSrtpCipher cipher;
  GstDtlsSrtpAuth auth;
  gint success;

  static gchar export_string[] = "EXTRACTOR-dtls_srtp";

  success = SSL_export_keying_material (self->priv->ssl,
      (gpointer) & exported_keys, 60, export_string, strlen (export_string),
      NULL, 0, 0);

  if (!success) {
    GST_WARNING_OBJECT (self, "failed to export srtp keys");
    return;
  }

  profile = SSL_get_selected_srtp_profile (self->priv->ssl);

  GST_INFO_OBJECT (self, "keys received, profile is %s", profile->name);

  switch (profile->id) {
    case SRTP_AES128_CM_SHA1_80:
      cipher = GST_DTLS_SRTP_CIPHER_AES_128_ICM;
      auth = GST_DTLS_SRTP_AUTH_HMAC_SHA1_80;
      break;
    case SRTP_AES128_CM_SHA1_32:
      cipher = GST_DTLS_SRTP_CIPHER_AES_128_ICM;
      auth = GST_DTLS_SRTP_AUTH_HMAC_SHA1_32;
      break;
    default:
      GST_WARNING_OBJECT (self, "invalid crypto suite set by handshake");
      goto beach;
  }

  client_key.key = exported_keys.client_key;
  server_key.key = exported_keys.server_key;
  client_key.salt = exported_keys.client_salt;
  server_key.salt = exported_keys.server_salt;

  if (self->priv->is_client) {
    g_signal_emit (self, signals[SIGNAL_ON_ENCODER_KEY], 0, &client_key, cipher,
        auth);
    g_signal_emit (self, signals[SIGNAL_ON_DECODER_KEY], 0, &server_key,
        cipher, auth);
  } else {
    g_signal_emit (self, signals[SIGNAL_ON_ENCODER_KEY], 0, &server_key,
        cipher, auth);
    g_signal_emit (self, signals[SIGNAL_ON_DECODER_KEY], 0, &client_key, cipher,
        auth);
  }

beach:
  self->priv->keys_exported = TRUE;
}

static void
openssl_poll (GstDtlsConnection * self)
{
  int ret;
  char buf[512];
  int error;

  log_state (self, "poll: before handshake");

  ret = SSL_do_handshake (self->priv->ssl);

  log_state (self, "poll: after handshake");

  if (ret == 1) {
    if (!self->priv->keys_exported) {
      GST_INFO_OBJECT (self,
          "handshake just completed successfully, exporting keys");
      export_srtp_keys (self);
    } else {
      GST_INFO_OBJECT (self, "handshake is completed");
    }
    return;
  } else {
    if (ret == 0) {
      GST_DEBUG_OBJECT (self, "do_handshake encountered EOF");
    } else if (ret == -1) {
      GST_WARNING_OBJECT (self, "do_handshake encountered BIO error");
    } else {
      GST_DEBUG_OBJECT (self, "do_handshake returned %d", ret);
    }
  }

  error = SSL_get_error (self->priv->ssl, ret);

  switch (error) {
    case SSL_ERROR_NONE:
      GST_WARNING_OBJECT (self, "no error, handshake should be done");
      break;
    case SSL_ERROR_SSL:
      GST_LOG_OBJECT (self, "SSL error %d: %s", error,
          ERR_error_string (ERR_get_error (), buf));
      break;
    case SSL_ERROR_WANT_READ:
      GST_LOG_OBJECT (self, "SSL wants read");
      break;
    case SSL_ERROR_WANT_WRITE:
      GST_LOG_OBJECT (self, "SSL wants write");
      break;
    case SSL_ERROR_SYSCALL:{
      GST_LOG_OBJECT (self, "SSL syscall (error) : %lu", ERR_get_error ());
      break;
    }
    default:
      GST_WARNING_OBJECT (self, "Unknown SSL error: %d, ret: %d", error, ret);
  }
}

static int
openssl_verify_callback (int preverify_ok, X509_STORE_CTX * x509_ctx)
{
  GstDtlsConnection *self;
  SSL *ssl;
  BIO *bio;
  gchar *pem = NULL;
  gboolean accepted = FALSE;

  ssl =
      X509_STORE_CTX_get_ex_data (x509_ctx,
      SSL_get_ex_data_X509_STORE_CTX_idx ());
  self = SSL_get_ex_data (ssl, connection_ex_index);
  g_return_val_if_fail (GST_IS_DTLS_CONNECTION (self), FALSE);

  pem = _gst_dtls_x509_to_pem (X509_STORE_CTX_get0_cert (x509_ctx));

  if (!pem) {
    GST_WARNING_OBJECT (self,
        "failed to convert received certificate to pem format");
  } else {
    bio = BIO_new (BIO_s_mem ());
    if (bio) {
      gchar buffer[2048];
      gint len;

      len =
          X509_NAME_print_ex (bio,
          X509_get_subject_name (X509_STORE_CTX_get0_cert (x509_ctx)), 1,
          XN_FLAG_MULTILINE);
      BIO_read (bio, buffer, len);
      buffer[len] = '\0';
      GST_DEBUG_OBJECT (self, "Peer certificate received:\n%s", buffer);
      BIO_free (bio);
    } else {
      GST_DEBUG_OBJECT (self, "failed to create certificate print membio");
    }

    g_signal_emit (self, signals[SIGNAL_ON_PEER_CERTIFICATE], 0, pem,
        &accepted);
    g_free (pem);
  }

  return accepted;
}

/*
    ########  ####  #######
    ##     ##  ##  ##     ##
    ##     ##  ##  ##     ##
    ########   ##  ##     ##
    ##     ##  ##  ##     ##
    ##     ##  ##  ##     ##
    ########  ####  #######
*/

#if OPENSSL_VERSION_NUMBER < 0x10100001L
static BIO_METHOD custom_bio_methods = {
  BIO_TYPE_BIO,
  "stream",
  bio_method_write,
  bio_method_read,
  NULL,
  NULL,
  bio_method_ctrl,
  bio_method_new,
  bio_method_free,
  NULL,
};

static BIO_METHOD *
BIO_s_gst_dtls_connection (void)
{
  return &custom_bio_methods;
}
#else
static BIO_METHOD *custom_bio_methods;

static BIO_METHOD *
BIO_s_gst_dtls_connection (void)
{
  if (custom_bio_methods != NULL)
    return custom_bio_methods;

  custom_bio_methods = BIO_meth_new (BIO_TYPE_BIO, "stream");
  if (custom_bio_methods == NULL
      || !BIO_meth_set_write (custom_bio_methods, bio_method_write)
      || !BIO_meth_set_read (custom_bio_methods, bio_method_read)
      || !BIO_meth_set_ctrl (custom_bio_methods, bio_method_ctrl)
      || !BIO_meth_set_create (custom_bio_methods, bio_method_new)
      || !BIO_meth_set_destroy (custom_bio_methods, bio_method_free)) {
    BIO_meth_free (custom_bio_methods);
    return NULL;
  }

  return custom_bio_methods;
}
#endif

static int
bio_method_write (BIO * bio, const char *data, int size)
{
  GstDtlsConnection *self = GST_DTLS_CONNECTION (BIO_get_data (bio));

  GST_LOG_OBJECT (self, "BIO: writing %d", size);

  if (self->priv->send_closure) {
    GValue values[3] = { G_VALUE_INIT };

    g_value_init (&values[0], GST_TYPE_DTLS_CONNECTION);
    g_value_set_object (&values[0], self);

    g_value_init (&values[1], G_TYPE_POINTER);
    g_value_set_pointer (&values[1], (gpointer) data);

    g_value_init (&values[2], G_TYPE_INT);
    g_value_set_int (&values[2], size);

    g_closure_invoke (self->priv->send_closure, NULL, 3, values, NULL);
  }

  return size;
}

static int
bio_method_read (BIO * bio, char *out_buffer, int size)
{
  GstDtlsConnection *self = GST_DTLS_CONNECTION (BIO_get_data (bio));
  GstDtlsConnectionPrivate *priv = self->priv;
  guint internal_size;
  gint copy_size;

  internal_size = priv->bio_buffer_len - priv->bio_buffer_offset;

  if (!priv->bio_buffer) {
    GST_LOG_OBJECT (self, "BIO: EOF");
    return 0;
  }

  if (!out_buffer || size <= 0) {
    GST_WARNING_OBJECT (self, "BIO: read got invalid arguments");
    if (internal_size) {
      BIO_set_retry_read (bio);
    }
    return internal_size;
  }

  if (size > internal_size) {
    copy_size = internal_size;
  } else {
    copy_size = size;
  }

  GST_DEBUG_OBJECT (self,
      "reading %d/%d bytes %d at offset %d, output buff size is %d", copy_size,
      priv->bio_buffer_len, internal_size, priv->bio_buffer_offset, size);

  memcpy (out_buffer, (guint8 *) priv->bio_buffer + priv->bio_buffer_offset,
      copy_size);
  priv->bio_buffer_offset += copy_size;

  if (priv->bio_buffer_len == priv->bio_buffer_offset) {
    priv->bio_buffer = NULL;
  }

  return copy_size;
}

static long
bio_method_ctrl (BIO * bio, int cmd, long arg1, void *arg2)
{
  GstDtlsConnection *self = GST_DTLS_CONNECTION (BIO_get_data (bio));
  GstDtlsConnectionPrivate *priv = self->priv;

  switch (cmd) {
    case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
    case BIO_CTRL_DGRAM_SET_RECV_TIMEOUT:
      GST_LOG_OBJECT (self, "BIO: Timeout set");
      gst_dtls_connection_check_timeout_locked (self);
      return 1;
    case BIO_CTRL_RESET:
      priv->bio_buffer = NULL;
      priv->bio_buffer_len = 0;
      priv->bio_buffer_offset = 0;
      GST_LOG_OBJECT (self, "BIO: EOF reset");
      return 1;
    case BIO_CTRL_EOF:{
      gint eof = !(priv->bio_buffer_len - priv->bio_buffer_offset);
      GST_LOG_OBJECT (self, "BIO: EOF query returned %d", eof);
      return eof;
    }
    case BIO_CTRL_WPENDING:
      GST_LOG_OBJECT (self, "BIO: pending write");
      return 1;
    case BIO_CTRL_PENDING:{
      gint pending = priv->bio_buffer_len - priv->bio_buffer_offset;
      GST_LOG_OBJECT (self, "BIO: %d bytes pending", pending);
      return pending;
    }
    case BIO_CTRL_FLUSH:
      GST_LOG_OBJECT (self, "BIO: flushing");
      return 1;
    case BIO_CTRL_DGRAM_QUERY_MTU:
      GST_DEBUG_OBJECT (self, "BIO: MTU query, returning 0...");
      return 0;
    case BIO_CTRL_DGRAM_MTU_EXCEEDED:
      GST_WARNING_OBJECT (self, "BIO: MTU exceeded");
      return 0;
    default:
      GST_LOG_OBJECT (self, "BIO: unhandled ctrl, %d", cmd);
      return 0;
  }
}

static int
bio_method_new (BIO * bio)
{
  GST_LOG_OBJECT (NULL, "BIO: new");

  BIO_set_shutdown (bio, 0);
  BIO_set_init (bio, 1);

  return 1;
}

static int
bio_method_free (BIO * bio)
{
  if (!bio) {
    GST_LOG_OBJECT (NULL, "BIO free called with null bio");
    return 0;
  }

  GST_LOG_OBJECT (GST_DTLS_CONNECTION (BIO_get_data (bio)), "BIO free");
  return 0;
}
