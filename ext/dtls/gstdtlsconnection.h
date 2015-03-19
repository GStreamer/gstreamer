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

#ifndef gstdtlsconnection_h
#define gstdtlsconnection_h

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_DTLS_CONNECTION            (gst_dtls_connection_get_type())
#define GST_DTLS_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DTLS_CONNECTION, GstDtlsConnection))
#define GST_DTLS_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DTLS_CONNECTION, GstDtlsConnectionClass))
#define GST_IS_DTLS_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DTLS_CONNECTION))
#define GST_IS_DTLS_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DTLS_CONNECTION))
#define GST_DTLS_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_DTLS_CONNECTION, GstDtlsConnectionClass))

typedef struct _GstDtlsConnection        GstDtlsConnection;
typedef struct _GstDtlsConnectionClass   GstDtlsConnectionClass;
typedef struct _GstDtlsConnectionPrivate GstDtlsConnectionPrivate;

/**
 * GstDtlsSrtpCipher:
 * @GST_DTLS_SRTP_CIPHER_AES_128_ICM: aes-128-icm
 *
 * SRTP Cipher selected by the DTLS handshake, should match the enums in gstsrtp
 */
typedef enum {
    GST_DTLS_SRTP_CIPHER_AES_128_ICM = 1
} GstDtlsSrtpCipher;

/**
 * GstDtlsSrtpAuth:
 * @GST_DTLS_SRTP_AUTH_HMAC_SHA1_32: hmac-sha1-32
 * @GST_DTLS_SRTP_AUTH_HMAC_SHA1_80: hmac-sha1-80
 *
 * SRTP Auth selected by the DTLS handshake, should match the enums in gstsrtp
 */
typedef enum {
    GST_DTLS_SRTP_AUTH_HMAC_SHA1_32 = 1,
    GST_DTLS_SRTP_AUTH_HMAC_SHA1_80 = 2
} GstDtlsSrtpAuth;

#define GST_DTLS_SRTP_MASTER_KEY_LENGTH 30

/*
 * GstDtlsConnection:
 *
 * A class that handles a single DTLS connection.
 * Any connection needs to be created with the agent property set.
 * Once the DTLS handshake is completed, on-encoder-key and on-decoder-key will be signalled.
 */
struct _GstDtlsConnection {
    GObject parent_instance;

    GstDtlsConnectionPrivate *priv;
};

struct _GstDtlsConnectionClass {
    GObjectClass parent_class;
};

GType gst_dtls_connection_get_type(void) G_GNUC_CONST;

void gst_dtls_connection_start(GstDtlsConnection *, gboolean is_client);
void gst_dtls_connection_check_timeout(GstDtlsConnection *);

/*
 * Stops the connections, it is not required to call this function.
 */
void gst_dtls_connection_stop(GstDtlsConnection *);

/*
 * Closes the connection, the function will block until the connection has been stopped.
 * If stop is called some time before, close will return instantly.
 */
void gst_dtls_connection_close(GstDtlsConnection *);

/*
 * Sets the closure that will be called whenever data needs to be sent.
 *
 * The closure will get called with the following arguments:
 * void cb(GstDtlsConnection *, gpointer data, gint length, gpointer user_data)
 */
void gst_dtls_connection_set_send_callback(GstDtlsConnection *, GClosure *);

/*
 * Processes data that has been recevied, the transformation is done in-place.
 * Returns the length of the plaintext data that was decoded, if no data is available, 0<= will be returned.
 */
gint gst_dtls_connection_process(GstDtlsConnection *, gpointer ptr, gint len);

/*
 * If the DTLS handshake is completed this function will encode the given data.
 * Returns the length of the data sent, or 0 if the DTLS handshake is not completed.
 */
gint gst_dtls_connection_send(GstDtlsConnection *, gpointer ptr, gint len);

G_END_DECLS

#endif /* gstdtlsconnection_h */
