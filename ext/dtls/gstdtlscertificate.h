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

#ifndef gstdtlscertificate_h
#define gstdtlscertificate_h

#include <glib-object.h>

G_BEGIN_DECLS

#define ER_TYPE_DTLS_CERTIFICATE            (er_dtls_certificate_get_type())
#define ER_DTLS_CERTIFICATE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), ER_TYPE_DTLS_CERTIFICATE, ErDtlsCertificate))
#define ER_DTLS_CERTIFICATE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), ER_TYPE_DTLS_CERTIFICATE, ErDtlsCertificateClass))
#define ER_IS_DTLS_CERTIFICATE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), ER_TYPE_DTLS_CERTIFICATE))
#define ER_IS_DTLS_CERTIFICATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), ER_TYPE_DTLS_CERTIFICATE))
#define ER_DTLS_CERTIFICATE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), ER_TYPE_DTLS_CERTIFICATE, ErDtlsCertificateClass))

typedef gpointer ErDtlsCertificateInternalCertificate;
typedef gpointer ErDtlsCertificateInternalKey;

typedef struct _ErDtlsCertificate        ErDtlsCertificate;
typedef struct _ErDtlsCertificateClass   ErDtlsCertificateClass;
typedef struct _ErDtlsCertificatePrivate ErDtlsCertificatePrivate;

/*
 * ErDtlsCertificate:
 *
 * Handles a X509 certificate and a private key.
 * If a certificate is created without the "pem" property, a self-signed certificate is generated.
 */
struct _ErDtlsCertificate {
    GObject parent_instance;

    ErDtlsCertificatePrivate *priv;
};

struct _ErDtlsCertificateClass {
    GObjectClass parent_class;
};

GType er_dtls_certificate_get_type(void) G_GNUC_CONST;

/* internal */
ErDtlsCertificateInternalCertificate _er_dtls_certificate_get_internal_certificate(ErDtlsCertificate *);
ErDtlsCertificateInternalKey _er_dtls_certificate_get_internal_key(ErDtlsCertificate *);
gchar *_er_dtls_x509_to_pem(gpointer x509);

G_END_DECLS

#endif /* gstdtlscertificate_h */
