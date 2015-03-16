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

#include "gstdtlsconnection.h"

#include "gstdtlsagent.h"
#include "gstdtlscertificate.h"
#include "gstdtlscommon.h"

#ifdef __APPLE__
# define __AVAILABILITYMACROS__
# define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>

#if ER_DTLS_USE_GST_LOG
    GST_DEBUG_CATEGORY_STATIC(er_dtls_connection_debug);
#   define GST_CAT_DEFAULT er_dtls_connection_debug
    G_DEFINE_TYPE_WITH_CODE(ErDtlsConnection, er_dtls_connection, G_TYPE_OBJECT,
        GST_DEBUG_CATEGORY_INIT(er_dtls_connection_debug, "gstdtlsconnection", 0, "Ericsson DTLS Connection"));
#else
    G_DEFINE_TYPE(ErDtlsConnection, er_dtls_connection, G_TYPE_OBJECT);
#endif

#define ER_DTLS_CONNECTION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), ER_TYPE_DTLS_CONNECTION, ErDtlsConnectionPrivate))

#define SRTP_KEY_LEN 16
#define SRTP_SALT_LEN 14

enum {
    SIGNAL_ON_ENCODER_KEY,
    SIGNAL_ON_DECODER_KEY,
    SIGNAL_ON_PEER_CERTIFICATE,
    NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

enum {
    PROP_0,
    PROP_AGENT,
    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

static int connection_ex_index;

struct _ErDtlsConnectionPrivate {
    SSL *ssl;
    BIO *bio;
    GThread *thread;

    gboolean is_client;
    gboolean is_alive;
    gboolean keys_exported;
    gboolean timeout_set;

    GMutex mutex;
    GCond condition;
    gpointer bio_buffer;
    gint bio_buffer_len;
    gint bio_buffer_offset;

    GClosure *send_closure;
};

static void er_dtls_connection_finalize(GObject *gobject);
static void er_dtls_connection_set_property(GObject *, guint prop_id, const GValue *, GParamSpec *);

static void log_state(ErDtlsConnection *, const gchar *str);
static gpointer connection_timeout_thread_func(ErDtlsConnection *);
static void export_srtp_keys(ErDtlsConnection *);
static void openssl_poll(ErDtlsConnection *);
static int openssl_verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx);

static BIO_METHOD* BIO_s_er_dtls_connection();
static int bio_method_write(BIO *, const char *data, int size);
static int bio_method_read(BIO *, char *out_buffer, int size);
static long bio_method_ctrl(BIO *, int cmd, long arg1, void *arg2);
static int bio_method_new(BIO *);
static int bio_method_free(BIO *);

static void er_dtls_connection_class_init(ErDtlsConnectionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(ErDtlsConnectionPrivate));

    gobject_class->set_property = er_dtls_connection_set_property;

    connection_ex_index = SSL_get_ex_new_index(0, "gstdtlsagent connection index", NULL, NULL, NULL);

    signals[SIGNAL_ON_DECODER_KEY] =
        g_signal_new("on-decoder-key", G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST, 0, NULL, NULL,
            g_cclosure_marshal_generic, G_TYPE_NONE, 3,
            G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT);

    signals[SIGNAL_ON_ENCODER_KEY] =
        g_signal_new("on-encoder-key", G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST, 0, NULL, NULL,
            g_cclosure_marshal_generic, G_TYPE_NONE, 3,
            G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT);

    signals[SIGNAL_ON_PEER_CERTIFICATE] =
        g_signal_new("on-peer-certificate", G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST, 0, NULL, NULL,
            g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 1,
            G_TYPE_STRING);

    properties[PROP_AGENT] =
        g_param_spec_object("agent",
            "ERDtlsAgent",
            "Agent to use in creation of the connection",
            ER_TYPE_DTLS_AGENT,
            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, NUM_PROPERTIES, properties);

    _er_dtls_init_openssl();

    gobject_class->finalize = er_dtls_connection_finalize;
}

static void er_dtls_connection_init(ErDtlsConnection *self)
{
    ErDtlsConnectionPrivate *priv = ER_DTLS_CONNECTION_GET_PRIVATE(self);
    self->priv = priv;

    priv->ssl = NULL;
    priv->bio = NULL;
    priv->thread = NULL;

    priv->send_closure = NULL;

    priv->is_client = FALSE;
    priv->is_alive = TRUE;
    priv->keys_exported = FALSE;
    priv->timeout_set = FALSE;

    priv->bio_buffer = NULL;
    priv->bio_buffer_len = 0;
    priv->bio_buffer_offset = 0;

    g_mutex_init(&priv->mutex);
    g_cond_init(&priv->condition);
}

static void er_dtls_connection_finalize(GObject *gobject)
{
    ErDtlsConnection *self = ER_DTLS_CONNECTION(gobject);
    ErDtlsConnectionPrivate *priv = self->priv;


    SSL_free(priv->ssl);
    priv->ssl = NULL;

    if (priv->send_closure) {
        g_closure_unref(priv->send_closure);
        priv->send_closure = NULL;
    }

    g_mutex_clear(&priv->mutex);
    g_cond_clear(&priv->condition);

    LOG_DEBUG(self, "finalized");

    G_OBJECT_CLASS(er_dtls_connection_parent_class)->finalize(gobject);
}

static void er_dtls_connection_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    ErDtlsConnection *self = ER_DTLS_CONNECTION(object);
    ErDtlsAgent *agent;
    ErDtlsConnectionPrivate *priv = self->priv;
    SSL_CTX *ssl_context;

    switch (prop_id) {
    case PROP_AGENT:
        g_return_if_fail(!priv->ssl);
        agent = ER_DTLS_AGENT(g_value_get_object(value));
        g_return_if_fail(ER_IS_DTLS_AGENT(agent));

        ssl_context = _er_dtls_agent_peek_context(agent);

        priv->ssl = SSL_new(ssl_context);
        g_return_if_fail(priv->ssl);

        priv->bio = BIO_new(BIO_s_er_dtls_connection());
        g_return_if_fail(priv->bio);

        priv->bio->ptr = self;
        SSL_set_bio(priv->ssl, priv->bio, priv->bio);

        SSL_set_verify(priv->ssl,
            SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, openssl_verify_callback);
        SSL_set_ex_data(priv->ssl, connection_ex_index, self);

        log_state(self, "connection created");
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
    }
}

void er_dtls_connection_start(ErDtlsConnection *self, gboolean is_client)
{
    g_return_if_fail(ER_IS_DTLS_CONNECTION(self));
    ErDtlsConnectionPrivate *priv = self->priv;
    g_return_if_fail(priv->send_closure);
    g_return_if_fail(priv->ssl);
    g_return_if_fail(priv->bio);

    LOG_TRACE(self, "locking @ start");
    g_mutex_lock(&priv->mutex);
    LOG_TRACE(self, "locked @ start");

    priv->is_alive = TRUE;
    priv->timeout_set = FALSE;
    priv->bio_buffer = NULL;
    priv->bio_buffer_len = 0;
    priv->bio_buffer_offset = 0;
    priv->keys_exported = FALSE;

    priv->is_client = is_client;
    if (priv->is_client) {
        SSL_set_connect_state(priv->ssl);
    } else {
        SSL_set_accept_state(priv->ssl);
    }
    log_state(self, "initial state set");

    openssl_poll(self);

    log_state(self, "first poll done");
    priv->thread = NULL;

    LOG_TRACE(self, "unlocking @ start");
    g_mutex_unlock(&priv->mutex);
}

void er_dtls_connection_start_timeout(ErDtlsConnection *self)
{
    g_return_if_fail(ER_IS_DTLS_CONNECTION(self));

    ErDtlsConnectionPrivate *priv = self->priv;
    GError *error = NULL;
    gchar *thread_name = g_strdup_printf("connection_thread_%p", self);

    LOG_TRACE(self, "locking @ start_timeout");
    g_mutex_lock(&priv->mutex);
    LOG_TRACE(self, "locked @ start_timeout");

    LOG_INFO(self, "starting connection timeout");
    priv->thread = g_thread_try_new(thread_name,
            (GThreadFunc) connection_timeout_thread_func, self, &error);
    if (error) {
        LOG_WARNING(self, "error creating connection thread: %s (%d)",
                error->message, error->code);
        g_clear_error(&error);
    }

    g_free(thread_name);

    LOG_TRACE(self, "unlocking @ start_timeout");
    g_mutex_unlock(&priv->mutex);
}

void er_dtls_connection_stop(ErDtlsConnection *self)
{
    g_return_if_fail(ER_IS_DTLS_CONNECTION(self));
    g_return_if_fail(self->priv->ssl);
    g_return_if_fail(self->priv->bio);

    LOG_DEBUG(self, "stopping connection");

    LOG_TRACE(self, "locking @ stop");
    g_mutex_lock(&self->priv->mutex);
    LOG_TRACE(self, "locked @ stop");

    self->priv->is_alive = FALSE;
    LOG_TRACE(self, "signaling @ stop");
    g_cond_signal(&self->priv->condition);
    LOG_TRACE(self, "signaled @ stop");

    LOG_TRACE(self, "unlocking @ stop");
    g_mutex_unlock(&self->priv->mutex);

    LOG_DEBUG(self, "stopped connection");
}

void er_dtls_connection_close(ErDtlsConnection *self)
{
    g_return_if_fail(ER_IS_DTLS_CONNECTION(self));
    g_return_if_fail(self->priv->ssl);
    g_return_if_fail(self->priv->bio);

    LOG_DEBUG(self, "closing connection");

    LOG_TRACE(self, "locking @ close");
    g_mutex_lock(&self->priv->mutex);
    LOG_TRACE(self, "locked @ close");

    if (self->priv->is_alive) {
        self->priv->is_alive = FALSE;
        g_cond_signal(&self->priv->condition);
    }

    LOG_TRACE(self, "unlocking @ close");
    g_mutex_unlock(&self->priv->mutex);

    if (self->priv->thread) {
        g_thread_join(self->priv->thread);
        self->priv->thread = NULL;
    }

    LOG_DEBUG(self, "closed connection");
}

void er_dtls_connection_set_send_callback(ErDtlsConnection *self, GClosure *closure)
{
    g_return_if_fail(ER_IS_DTLS_CONNECTION(self));

    LOG_TRACE(self, "locking @ set_send_callback");
    g_mutex_lock(&self->priv->mutex);
    LOG_TRACE(self, "locked @ set_send_callback");

    self->priv->send_closure = closure;

    if (closure && G_CLOSURE_NEEDS_MARSHAL(closure)) {
        g_closure_set_marshal(closure, g_cclosure_marshal_generic);
    }

    LOG_TRACE(self, "unlocking @ set_send_callback");
    g_mutex_unlock(&self->priv->mutex);
}

gint er_dtls_connection_process(ErDtlsConnection *self, gpointer data, gint len)
{
    g_return_val_if_fail(ER_IS_DTLS_CONNECTION(self), 0);
    ErDtlsConnectionPrivate *priv = self->priv;
    gint result;

    g_return_val_if_fail(self->priv->ssl, 0);
    g_return_val_if_fail(self->priv->bio, 0);

    LOG_TRACE(self, "locking @ process");
    g_mutex_lock(&priv->mutex);
    LOG_TRACE(self, "locked @ process");

    g_warn_if_fail(!priv->bio_buffer);

    priv->bio_buffer = data;
    priv->bio_buffer_len = len;
    priv->bio_buffer_offset = 0;

    log_state(self, "process start");

    if (SSL_want_write(priv->ssl)) {
        openssl_poll(self);
        log_state(self, "process want write, after poll");
    }

    result = SSL_read(priv->ssl, data, len);

    log_state(self, "process after read");

    openssl_poll(self);

    log_state(self, "process after poll");

    LOG_DEBUG(self, "read result: %d", result);

    LOG_TRACE(self, "unlocking @ process");
    g_mutex_unlock(&priv->mutex);

    return result;
}

gint er_dtls_connection_send(ErDtlsConnection *self, gpointer data, gint len)
{
    g_return_val_if_fail(ER_IS_DTLS_CONNECTION(self), 0);
    int ret = 0;

    g_return_val_if_fail(self->priv->ssl, 0);
    g_return_val_if_fail(self->priv->bio, 0);

    LOG_TRACE(self, "locking @ send");
    g_mutex_lock(&self->priv->mutex);
    LOG_TRACE(self, "locked @ send");

    if (SSL_is_init_finished(self->priv->ssl)) {
        ret = SSL_write(self->priv->ssl, data, len);
        LOG_DEBUG(self, "data sent: input was %d B, output is %d B", len, ret);
    } else {
        LOG_WARNING(self, "tried to send data before handshake was complete");
        ret = 0;
    }

    LOG_TRACE(self, "unlocking @ send");
    g_mutex_unlock(&self->priv->mutex);

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

static void log_state(ErDtlsConnection *self, const gchar *str)
{
    ErDtlsConnectionPrivate *priv = self->priv;
    guint states = 0;

    states |= (!!SSL_is_init_finished(priv->ssl) << 0);
    states |= (!!SSL_in_init(priv->ssl) << 4);
    states |= (!!SSL_in_before(priv->ssl) << 8);
    states |= (!!SSL_in_connect_init(priv->ssl) << 12);
    states |= (!!SSL_in_accept_init(priv->ssl) << 16);
    states |= (!!SSL_want_write(priv->ssl) << 20);
    states |= (!!SSL_want_read(priv->ssl) << 24);

    LOG_LOG(self, "%s: role=%s buf=(%d,%p:%d/%d) %x|%x %s",
        str,
        priv->is_client ? "client" : "server",
        pqueue_size(priv->ssl->d1->sent_messages),
        priv->bio_buffer,
        priv->bio_buffer_offset,
        priv->bio_buffer_len,
        states, SSL_get_state(priv->ssl),
        SSL_state_string_long(priv->ssl));
}

static gpointer connection_timeout_thread_func(ErDtlsConnection *self)
{
    ErDtlsConnectionPrivate *priv = self->priv;
    struct timeval timeout;
    gint64 end_time, wait_time;
    gint ret;

    while (priv->is_alive) {
        LOG_TRACE(self, "locking @ timeout");
        g_mutex_lock(&priv->mutex);
        LOG_TRACE(self, "locked @ timeout");

        if (DTLSv1_get_timeout(priv->ssl, &timeout)) {
            wait_time = timeout.tv_sec * G_USEC_PER_SEC + timeout.tv_usec;

            if (wait_time) {
                LOG_DEBUG(self, "waiting for %" G_GINT64_FORMAT " usec", wait_time);

                end_time = g_get_monotonic_time() + wait_time;

                LOG_TRACE(self, "wait @ timeout");
                g_cond_wait_until(&priv->condition, &priv->mutex, end_time);
                LOG_TRACE(self, "continued @ timeout");
            }

            ret = DTLSv1_handle_timeout(priv->ssl);

            LOG_DEBUG(self, "handle timeout returned %d, is_alive: %d", ret, priv->is_alive);

            if (ret < 0) {
                LOG_TRACE(self, "unlocking @ timeout failed");
                g_mutex_unlock(&priv->mutex);
                break; /* self failed after DTLS1_TMO_ALERT_COUNT (12) attempts */
            }

            if (ret > 0) {
                log_state(self, "handling timeout before poll");
                openssl_poll(self);
                log_state(self, "handling timeout after poll");
            }
        } else {
            LOG_DEBUG(self, "waiting indefinitely");

            priv->timeout_set = FALSE;

            while (!priv->timeout_set && priv->is_alive) {
                LOG_TRACE(self, "wait @ timeout");
                g_cond_wait(&priv->condition, &priv->mutex);
            }
            LOG_TRACE(self, "continued @ timeout");
        }

        LOG_TRACE(self, "unlocking @ timeout");
        g_mutex_unlock(&priv->mutex);
    }

    log_state(self, "timeout thread exiting");

    return NULL;
}

static void export_srtp_keys(ErDtlsConnection *self)
{
    typedef struct {
        guint8 v[SRTP_KEY_LEN];
    } Key;

    typedef struct {
        guint8 v[SRTP_SALT_LEN];
    } Salt;

    struct {
        Key client_key;
        Key server_key;
        Salt client_salt;
        Salt server_salt;
    } exported_keys;

    struct {
        Key key;
        Salt salt;
    } client_key, server_key;

    SRTP_PROTECTION_PROFILE *profile;
    ErDtlsSrtpCipher cipher;
    ErDtlsSrtpAuth auth;
    gint success;

    static gchar export_string[] = "EXTRACTOR-dtls_srtp";

    success = SSL_export_keying_material(self->priv->ssl,
        (gpointer) &exported_keys, 60, export_string, strlen(export_string), NULL, 0, 0);

    if (!success) {
        LOG_WARNING(self, "failed to export srtp keys");
        return;
    }

    profile = SSL_get_selected_srtp_profile(self->priv->ssl);

    LOG_INFO(self, "keys received, profile is %s", profile->name);

    switch (profile->id) {
    case SRTP_AES128_CM_SHA1_80:
        cipher = ER_DTLS_SRTP_CIPHER_AES_128_ICM;
        auth = ER_DTLS_SRTP_AUTH_HMAC_SHA1_80;
        break;
    case SRTP_AES128_CM_SHA1_32:
        cipher = ER_DTLS_SRTP_CIPHER_AES_128_ICM;
        auth = ER_DTLS_SRTP_AUTH_HMAC_SHA1_32;
        break;
    default:
        LOG_WARNING(self, "invalid crypto suite set by handshake");
        goto beach;
    }

    client_key.key = exported_keys.client_key;
    server_key.key = exported_keys.server_key;
    client_key.salt = exported_keys.client_salt;
    server_key.salt = exported_keys.server_salt;

    if (self->priv->is_client) {
        g_signal_emit(self, signals[SIGNAL_ON_ENCODER_KEY], 0, &client_key, cipher, auth);
        g_signal_emit(self, signals[SIGNAL_ON_DECODER_KEY], 0, &server_key, cipher, auth);
    } else {
        g_signal_emit(self, signals[SIGNAL_ON_ENCODER_KEY], 0, &server_key, cipher, auth);
        g_signal_emit(self, signals[SIGNAL_ON_DECODER_KEY], 0, &client_key, cipher, auth);
    }

beach:
    self->priv->keys_exported = TRUE;
}

static void openssl_poll(ErDtlsConnection *self)
{
    int ret;
    char buf[512];
    int error;

    log_state(self, "poll: before handshake");

    ret = SSL_do_handshake(self->priv->ssl);

    log_state(self, "poll: after handshake");

    if (ret == 1) {
        if (!self->priv->keys_exported) {
            LOG_INFO(self, "handshake just completed successfully, exporting keys");
            export_srtp_keys(self);
        } else {
            LOG_INFO(self, "handshake is completed");
        }
        return;
    } else {
        if (ret == 0) {
            LOG_DEBUG(self, "do_handshake encountered EOF");
        } else if (ret == -1) {
            LOG_WARNING(self, "do_handshake encountered BIO error");
        } else {
            LOG_DEBUG(self, "do_handshake returned %d", ret);
        }
    }

    error = SSL_get_error(self->priv->ssl, ret);

    switch (error) {
    case SSL_ERROR_NONE:
        LOG_WARNING(self, "no error, handshake should be done");
        break;
    case SSL_ERROR_SSL:
        LOG_LOG(self, "SSL error %d: %s", error, ERR_error_string(ERR_get_error(), buf));
        break;
    case SSL_ERROR_WANT_READ:
        LOG_LOG(self, "SSL wants read");
        break;
    case SSL_ERROR_WANT_WRITE:
        LOG_LOG(self, "SSL wants write");
        break;
    case SSL_ERROR_SYSCALL: {
        LOG_LOG(self, "SSL syscall (error) : %lu", ERR_get_error());
        break;
    }
    default:
        LOG_WARNING(self, "Unknown SSL error: %d, ret: %d", error, ret);
    }
}

static int openssl_verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
    ErDtlsConnection *self;
    SSL *ssl;
    BIO *bio;
    gchar *pem = NULL;
    gboolean accepted = FALSE;

    ssl = X509_STORE_CTX_get_ex_data(x509_ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    self = SSL_get_ex_data(ssl, connection_ex_index);
    g_return_val_if_fail(ER_IS_DTLS_CONNECTION(self), FALSE);

    pem = _er_dtls_x509_to_pem(x509_ctx->cert);

    if (!pem) {
        LOG_WARNING(self, "failed to convert received certificate to pem format");
    } else {
        bio = BIO_new(BIO_s_mem());
        if (bio) {
            gchar buffer[2048];
            gint len;

            len = X509_NAME_print_ex(bio, X509_get_subject_name(x509_ctx->cert), 1, XN_FLAG_MULTILINE);
            BIO_read(bio, buffer, len);
            buffer[len] = '\0';
            LOG_DEBUG(self, "Peer certificate received:\n%s", buffer);
            BIO_free(bio);
        } else {
            LOG_DEBUG(self, "failed to create certificate print membio");
        }

        g_signal_emit(self, signals[SIGNAL_ON_PEER_CERTIFICATE], 0, pem, &accepted);
        g_free(pem);
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

static BIO_METHOD* BIO_s_er_dtls_connection()
{
    return &custom_bio_methods;
}

static int bio_method_write(BIO *bio, const char *data, int size)
{
    ErDtlsConnection *self = ER_DTLS_CONNECTION(bio->ptr);

    LOG_LOG(self, "BIO: writing %d", size);

    if (self->priv->send_closure) {
        GValue values[3] = { G_VALUE_INIT };

        g_value_init(&values[0], ER_TYPE_DTLS_CONNECTION);
        g_value_set_object(&values[0], self);

        g_value_init(&values[1], G_TYPE_POINTER);
        g_value_set_pointer(&values[1], (gpointer) data);

        g_value_init(&values[2], G_TYPE_INT);
        g_value_set_int(&values[2], size);

        g_closure_invoke(self->priv->send_closure, NULL, 3, values, NULL);
    }

    return size;
}

static int bio_method_read(BIO *bio, char *out_buffer, int size)
{
    ErDtlsConnection *self = ER_DTLS_CONNECTION(bio->ptr);
    ErDtlsConnectionPrivate *priv = self->priv;
    guint internal_size;
    gint copy_size;

    internal_size = priv->bio_buffer_len - priv->bio_buffer_offset;

    if (!priv->bio_buffer) {
        LOG_LOG(self, "BIO: EOF");
        return 0;
    }

    if (!out_buffer || size <= 0) {
        LOG_WARNING(self, "BIO: read got invalid arguments");
        if (internal_size) {
            BIO_set_retry_read(bio);
        }
        return internal_size;
    }

    if (size > internal_size) {
        copy_size = internal_size;
    } else {
        copy_size = size;
    }

    LOG_DEBUG(self, "reading %d/%d bytes %d at offset %d, output buff size is %d",
        copy_size, priv->bio_buffer_len, internal_size, priv->bio_buffer_offset, size);

    memcpy(out_buffer, priv->bio_buffer + priv->bio_buffer_offset, copy_size);
    priv->bio_buffer_offset += copy_size;

    if (priv->bio_buffer_len == priv->bio_buffer_offset) {
        priv->bio_buffer = NULL;
    }

    return copy_size;
}

static long bio_method_ctrl(BIO *bio, int cmd, long arg1, void *arg2)
{
    ErDtlsConnection *self = ER_DTLS_CONNECTION(bio->ptr);
    ErDtlsConnectionPrivate *priv = self->priv;

    switch (cmd) {
    case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
    case BIO_CTRL_DGRAM_SET_RECV_TIMEOUT:
        LOG_LOG(self, "BIO: Timeout set");
        priv->timeout_set = TRUE;
        g_cond_signal(&priv->condition);
        return 1;
    case BIO_CTRL_RESET:
        priv->bio_buffer = NULL;
        priv->bio_buffer_len = 0;
        priv->bio_buffer_offset = 0;
        LOG_LOG(self, "BIO: EOF reset");
        return 1;
    case BIO_CTRL_EOF: {
        gint eof = !(priv->bio_buffer_len - priv->bio_buffer_offset);
        LOG_LOG(self, "BIO: EOF query returned %d", eof);
        return eof;
    }
    case BIO_CTRL_WPENDING:
        LOG_LOG(self, "BIO: pending write");
        return 1;
    case BIO_CTRL_PENDING: {
        gint pending = priv->bio_buffer_len - priv->bio_buffer_offset;
        LOG_LOG(self, "BIO: %d bytes pending", pending);
        return pending;
    }
    case BIO_CTRL_FLUSH:
        LOG_LOG(self, "BIO: flushing");
        return 1;
    case BIO_CTRL_DGRAM_QUERY_MTU:
        LOG_DEBUG(self, "BIO: MTU query, returning 0...");
        return 0;
    case BIO_CTRL_DGRAM_MTU_EXCEEDED:
        LOG_WARNING(self, "BIO: MTU exceeded");
        return 0;
    default:
        LOG_LOG(self, "BIO: unhandled ctrl, %d", cmd);
        return 0;
    }
}

static int bio_method_new(BIO *bio)
{
    LOG_LOG(NULL, "BIO: new");

    bio->shutdown = 0;
    bio->init = 1;

    return 1;
}

static int bio_method_free(BIO *bio)
{
    if (!bio) {
        LOG_LOG(NULL, "BIO free called with null bio");
        return 0;
    }

    LOG_LOG(ER_DTLS_CONNECTION(bio->ptr), "BIO free");
    return 0;
}
