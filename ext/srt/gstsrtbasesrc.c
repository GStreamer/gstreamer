/* GStreamer SRT plugin based on libsrt
 * Copyright (C) 2017, Collabora Ltd.
 *   Author:Justin Kim <justin.kim@collabora.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsrtbasesrc.h"
#include "gstsrt.h"
#include <srt/srt.h>
#include <gio/gio.h>

#define GST_CAT_DEFAULT gst_debug_srt_base_src
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

enum
{
  PROP_URI = 1,
  PROP_CAPS,
  PROP_LATENCY,
  PROP_PASSPHRASE,
  PROP_KEY_LENGTH,

  /*< private > */
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void gst_srt_base_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static gchar *gst_srt_base_src_uri_get_uri (GstURIHandler * handler);
static gboolean gst_srt_base_src_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error);

#define gst_srt_base_src_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstSRTBaseSrc, gst_srt_base_src,
    GST_TYPE_PUSH_SRC, G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_srt_base_src_uri_handler_init)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "srtbasesrc", 0,
        "SRT Base Source"));

static void
gst_srt_base_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSRTBaseSrc *self = GST_SRT_BASE_SRC (object);

  switch (prop_id) {
    case PROP_URI:
      if (self->uri != NULL) {
        gchar *uri_str = gst_srt_base_src_uri_get_uri (GST_URI_HANDLER (self));
        g_value_take_string (value, uri_str);
      }
      break;
    case PROP_CAPS:
      GST_OBJECT_LOCK (self);
      gst_value_set_caps (value, self->caps);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_LATENCY:
      g_value_set_int (value, self->latency);
      break;
    case PROP_PASSPHRASE:
      g_value_set_string (value, self->passphrase);
      break;
    case PROP_KEY_LENGTH:
      g_value_set_int (value, self->key_length);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_base_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstSRTBaseSrc *self = GST_SRT_BASE_SRC (object);

  switch (prop_id) {
    case PROP_URI:
      gst_srt_base_src_uri_set_uri (GST_URI_HANDLER (self),
          g_value_get_string (value), NULL);
      break;
    case PROP_CAPS:
      GST_OBJECT_LOCK (self);
      g_clear_pointer (&self->caps, gst_caps_unref);
      self->caps = gst_caps_copy (gst_value_get_caps (value));
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_LATENCY:
      self->latency = g_value_get_int (value);
      break;
    case PROP_PASSPHRASE:
      g_free (self->passphrase);
      self->passphrase = g_value_dup_string (value);
      break;
    case PROP_KEY_LENGTH:
    {
      gint key_length = g_value_get_int (value);
      g_return_if_fail (key_length == 16 || key_length == 24
          || key_length == 32);
      self->key_length = key_length;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_base_src_finalize (GObject * object)
{
  GstSRTBaseSrc *self = GST_SRT_BASE_SRC (object);

  g_clear_pointer (&self->uri, gst_uri_unref);
  g_clear_pointer (&self->caps, gst_caps_unref);
  g_clear_pointer (&self->passphrase, g_free);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_srt_base_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstSRTBaseSrc *self = GST_SRT_BASE_SRC (src);
  GstCaps *result, *caps = NULL;

  GST_OBJECT_LOCK (self);
  if (self->caps != NULL) {
    caps = gst_caps_ref (self->caps);
  }
  GST_OBJECT_UNLOCK (self);

  if (caps) {
    if (filter) {
      result = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (caps);
    } else {
      result = caps;
    }
  } else {
    result = (filter) ? gst_caps_ref (filter) : gst_caps_new_any ();
  }

  return result;
}


static void
gst_srt_base_src_class_init (GstSRTBaseSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_srt_base_src_set_property;
  gobject_class->get_property = gst_srt_base_src_get_property;
  gobject_class->finalize = gst_srt_base_src_finalize;

  /**
   * GstSRTBaseSrc:uri:
   * 
   * The URI used by SRT Connection.
   */
  properties[PROP_URI] = g_param_spec_string ("uri", "URI",
      "URI in the form of srt://address:port", SRT_DEFAULT_URI,
      G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY | G_PARAM_STATIC_STRINGS);

  /**
   * GstSRTBaseSrc:caps:
   *
   * The Caps used by the source pad.
   */
  properties[PROP_CAPS] =
      g_param_spec_boxed ("caps", "Caps", "The caps of the source pad",
      GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_LATENCY] =
      g_param_spec_int ("latency", "latency",
      "Minimum latency (milliseconds)", 0,
      G_MAXINT32, SRT_DEFAULT_LATENCY,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PASSPHRASE] = g_param_spec_string ("passphrase", "Passphrase",
      "The password for the encrypted transmission", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_KEY_LENGTH] =
      g_param_spec_int ("key-length", "key length",
      "Crypto key length in bytes{16,24,32}", 16,
      32, SRT_DEFAULT_KEY_LENGTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_srt_base_src_get_caps);
}

static void
gst_srt_base_src_init (GstSRTBaseSrc * self)
{
  gst_srt_base_src_uri_set_uri (GST_URI_HANDLER (self), SRT_DEFAULT_URI, NULL);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  self->latency = SRT_DEFAULT_LATENCY;
  self->key_length = SRT_DEFAULT_KEY_LENGTH;
}

static GstURIType
gst_srt_base_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_srt_base_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { SRT_URI_SCHEME, NULL };

  return protocols;
}

static gchar *
gst_srt_base_src_uri_get_uri (GstURIHandler * handler)
{
  gchar *uri_str;
  GstSRTBaseSrc *self = GST_SRT_BASE_SRC (handler);

  GST_OBJECT_LOCK (self);
  uri_str = gst_uri_to_string (self->uri);
  GST_OBJECT_UNLOCK (self);

  return uri_str;
}

static gboolean
gst_srt_base_src_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error)
{
  GstSRTBaseSrc *self = GST_SRT_BASE_SRC (handler);
  gboolean ret = TRUE;
  GstUri *parsed_uri = gst_uri_from_string (uri);

  if (g_strcmp0 (gst_uri_get_scheme (parsed_uri), SRT_URI_SCHEME) != 0) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid SRT URI scheme");
    ret = FALSE;
    goto out;
  }

  GST_OBJECT_LOCK (self);

  g_clear_pointer (&self->uri, gst_uri_unref);
  self->uri = gst_uri_ref (parsed_uri);

  GST_OBJECT_UNLOCK (self);

out:
  g_clear_pointer (&parsed_uri, gst_uri_unref);
  return ret;
}

static void
gst_srt_base_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_srt_base_src_uri_get_type;
  iface->get_protocols = gst_srt_base_src_uri_get_protocols;
  iface->get_uri = gst_srt_base_src_uri_get_uri;
  iface->set_uri = gst_srt_base_src_uri_set_uri;
}
