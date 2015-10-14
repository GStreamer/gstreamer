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

#include "gstdtlscertificate.h"

#include "gstdtlsagent.h"

#ifdef __APPLE__
# define __AVAILABILITYMACROS__
# define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#endif

#ifdef G_OS_WIN32
#include <windows.h>
#ifdef X509_NAME
#undef X509_NAME
#endif
#endif

#include <openssl/ssl.h>

GST_DEBUG_CATEGORY_STATIC (gst_dtls_certificate_debug);
#define GST_CAT_DEFAULT gst_dtls_certificate_debug

G_DEFINE_TYPE_WITH_CODE (GstDtlsCertificate, gst_dtls_certificate,
    G_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (gst_dtls_certificate_debug,
        "dtlscertificate", 0, "DTLS Certificate"));

#define GST_DTLS_CERTIFICATE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), GST_TYPE_DTLS_CERTIFICATE, GstDtlsCertificatePrivate))

enum
{
  PROP_0,
  PROP_PEM,
  NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

#define DEFAULT_PEM NULL

struct _GstDtlsCertificatePrivate
{
  X509 *x509;
  EVP_PKEY *private_key;

  gchar *pem;
};

static void gst_dtls_certificate_finalize (GObject * gobject);
static void gst_dtls_certificate_set_property (GObject *, guint prop_id,
    const GValue *, GParamSpec *);
static void gst_dtls_certificate_get_property (GObject *, guint prop_id,
    GValue *, GParamSpec *);

static void init_generated (GstDtlsCertificate *);
static void init_from_pem_string (GstDtlsCertificate *, const gchar * pem);

static void
gst_dtls_certificate_class_init (GstDtlsCertificateClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstDtlsCertificatePrivate));

  gobject_class->set_property = gst_dtls_certificate_set_property;
  gobject_class->get_property = gst_dtls_certificate_get_property;

  properties[PROP_PEM] =
      g_param_spec_string ("pem",
      "Pem string",
      "A string containing a X509 certificate and RSA private key in PEM format",
      DEFAULT_PEM,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, NUM_PROPERTIES, properties);

  _gst_dtls_init_openssl ();

  gobject_class->finalize = gst_dtls_certificate_finalize;
}

static void
gst_dtls_certificate_init (GstDtlsCertificate * self)
{
  GstDtlsCertificatePrivate *priv = GST_DTLS_CERTIFICATE_GET_PRIVATE (self);
  self->priv = priv;

  priv->x509 = NULL;
  priv->private_key = NULL;
  priv->pem = NULL;
}

static void
gst_dtls_certificate_finalize (GObject * gobject)
{
  GstDtlsCertificatePrivate *priv = GST_DTLS_CERTIFICATE (gobject)->priv;

  X509_free (priv->x509);
  priv->x509 = NULL;

  EVP_PKEY_free (priv->private_key);
  priv->private_key = NULL;


  g_free (priv->pem);
  priv->pem = NULL;

  G_OBJECT_CLASS (gst_dtls_certificate_parent_class)->finalize (gobject);
}

static void
gst_dtls_certificate_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDtlsCertificate *self = GST_DTLS_CERTIFICATE (object);
  const gchar *pem;

  switch (prop_id) {
    case PROP_PEM:
      pem = g_value_get_string (value);
      if (pem) {
        init_from_pem_string (self, pem);
      } else {
        init_generated (self);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
  }
}

static void
gst_dtls_certificate_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDtlsCertificate *self = GST_DTLS_CERTIFICATE (object);

  switch (prop_id) {
    case PROP_PEM:
      g_return_if_fail (self->priv->pem);
      g_value_set_string (value, self->priv->pem);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
  }
}

static void
init_generated (GstDtlsCertificate * self)
{
  GstDtlsCertificatePrivate *priv = self->priv;
  RSA *rsa;
  X509_NAME *name = NULL;

  g_return_if_fail (!priv->x509);
  g_return_if_fail (!priv->private_key);

  priv->private_key = EVP_PKEY_new ();

  if (!priv->private_key) {
    GST_WARNING_OBJECT (self, "failed to create private key");
    return;
  }

  priv->x509 = X509_new ();

  if (!priv->x509) {
    GST_WARNING_OBJECT (self, "failed to create certificate");
    EVP_PKEY_free (priv->private_key);
    priv->private_key = NULL;
    return;
  }
  rsa = RSA_generate_key (2048, RSA_F4, NULL, NULL);

  if (!rsa) {
    GST_WARNING_OBJECT (self, "failed to generate RSA");
    EVP_PKEY_free (priv->private_key);
    priv->private_key = NULL;
    X509_free (priv->x509);
    priv->x509 = NULL;
    return;
  }

  if (!EVP_PKEY_assign_RSA (priv->private_key, rsa)) {
    GST_WARNING_OBJECT (self, "failed to assign RSA");
    RSA_free (rsa);
    rsa = NULL;
    EVP_PKEY_free (priv->private_key);
    priv->private_key = NULL;
    X509_free (priv->x509);
    priv->x509 = NULL;
    return;
  }
  rsa = NULL;

  X509_set_version (priv->x509, 2);
  ASN1_INTEGER_set (X509_get_serialNumber (priv->x509), 0);
  X509_gmtime_adj (X509_get_notBefore (priv->x509), 0);
  X509_gmtime_adj (X509_get_notAfter (priv->x509), 31536000L);  /* A year */
  X509_set_pubkey (priv->x509, priv->private_key);

  name = X509_get_subject_name (priv->x509);
  X509_NAME_add_entry_by_txt (name, "C", MBSTRING_ASC, (unsigned char *) "SE",
      -1, -1, 0);
  X509_NAME_add_entry_by_txt (name, "CN", MBSTRING_ASC,
      (unsigned char *) "OpenWebRTC", -1, -1, 0);
  X509_set_issuer_name (priv->x509, name);
  name = NULL;

  if (!X509_sign (priv->x509, priv->private_key, EVP_sha256 ())) {
    GST_WARNING_OBJECT (self, "failed to sign certificate");
    EVP_PKEY_free (priv->private_key);
    priv->private_key = NULL;
    X509_free (priv->x509);
    priv->x509 = NULL;
    return;
  }

  self->priv->pem = _gst_dtls_x509_to_pem (priv->x509);
}

static void
init_from_pem_string (GstDtlsCertificate * self, const gchar * pem)
{
  GstDtlsCertificatePrivate *priv = self->priv;
  BIO *bio;

  g_return_if_fail (pem);
  g_return_if_fail (!priv->x509);
  g_return_if_fail (!priv->private_key);

  bio = BIO_new_mem_buf ((gpointer) pem, -1);
  g_return_if_fail (bio);

  priv->x509 = PEM_read_bio_X509 (bio, NULL, NULL, NULL);

  if (!priv->x509) {
    GST_WARNING_OBJECT (self, "failed to read certificate from pem string");
    return;
  }

  (void) BIO_reset (bio);

  priv->private_key = PEM_read_bio_PrivateKey (bio, NULL, NULL, NULL);

  BIO_free (bio);
  bio = NULL;

  if (!priv->private_key) {
    GST_WARNING_OBJECT (self, "failed to read private key from pem string");
    X509_free (priv->x509);
    priv->x509 = NULL;
    return;
  }

  self->priv->pem = g_strdup (pem);
}

gchar *
_gst_dtls_x509_to_pem (gpointer x509)
{
#define GST_DTLS_BIO_BUFFER_SIZE 4096
  BIO *bio;
  gchar buffer[GST_DTLS_BIO_BUFFER_SIZE] = { 0 };
  gint len;
  gchar *pem = NULL;

  bio = BIO_new (BIO_s_mem ());
  g_return_val_if_fail (bio, NULL);

  if (!PEM_write_bio_X509 (bio, (X509 *) x509)) {
    g_warn_if_reached ();
    goto beach;
  }

  len = BIO_read (bio, buffer, GST_DTLS_BIO_BUFFER_SIZE);
  if (!len) {
    g_warn_if_reached ();
    goto beach;
  }

  pem = g_strndup (buffer, len);

beach:
  BIO_free (bio);

  return pem;
}

GstDtlsCertificateInternalCertificate
_gst_dtls_certificate_get_internal_certificate (GstDtlsCertificate * self)
{
  g_return_val_if_fail (GST_IS_DTLS_CERTIFICATE (self), NULL);
  return self->priv->x509;
}

GstDtlsCertificateInternalKey
_gst_dtls_certificate_get_internal_key (GstDtlsCertificate * self)
{
  g_return_val_if_fail (GST_IS_DTLS_CERTIFICATE (self), NULL);
  return self->priv->private_key;
}
