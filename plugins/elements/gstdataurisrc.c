/* GStreamer data:// uri source element
 * Copyright (C) 2009 Igalia S.L
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-dataurisrc
 * @title: dataurisrc
 *
 * dataurisrc handles data: URIs, see [RFC 2397](http://tools.ietf.org/html/rfc2397) for more information.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 -v dataurisrc uri="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAfElEQVQ4je2MwQnAIAxFgziA4EnczIsO4MEROo/gzZWc4xdTbe1R6LGRR74heYS7iKElzfcMiRnt4hf8gk8EayB6luefue/HzlJfCA50XsNjYRxprZmenXNIKSGEsC+QUqK1hhgj521BzhnWWiilUGvdF5RS4L2HMQZCCJy8sHMm2TYdJAAAAABJRU5ErkJggg==" ! pngdec ! videoconvert ! imagefreeze ! videoconvert ! autovideosink
 * ]|
 *
 * This pipeline displays a small 16x16 PNG image from the data URI.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdataurisrc.h"

#include <string.h>
#include <gst/base/gsttypefindhelper.h>

GST_DEBUG_CATEGORY (data_uri_src_debug);
#define GST_CAT_DEFAULT (data_uri_src_debug)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_URI,
};

static void gst_data_uri_src_finalize (GObject * object);
static void gst_data_uri_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_data_uri_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstCaps *gst_data_uri_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_data_uri_src_get_size (GstBaseSrc * src, guint64 * size);
static gboolean gst_data_uri_src_is_seekable (GstBaseSrc * src);
static GstFlowReturn gst_data_uri_src_create (GstBaseSrc * src, guint64 offset,
    guint size, GstBuffer ** buf);
static gboolean gst_data_uri_src_start (GstBaseSrc * src);

static void gst_data_uri_src_handler_init (gpointer g_iface,
    gpointer iface_data);
static GstURIType gst_data_uri_src_get_uri_type (GType type);
static const gchar *const *gst_data_uri_src_get_protocols (GType type);
static gchar *gst_data_uri_src_get_uri (GstURIHandler * handler);
static gboolean gst_data_uri_src_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error);


#define gst_data_uri_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDataURISrc, gst_data_uri_src, GST_TYPE_BASE_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_data_uri_src_handler_init));

static void
gst_data_uri_src_class_init (GstDataURISrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseSrcClass *basesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->finalize = gst_data_uri_src_finalize;
  gobject_class->set_property = gst_data_uri_src_set_property;
  gobject_class->get_property = gst_data_uri_src_get_property;

  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri",
          "URI",
          "URI that should be used",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class,
      "data: URI source element", "Source", "Handles data: uris",
      "Philippe Normand <pnormand@igalia.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (data_uri_src_debug, "dataurisrc", 0,
      "data: URI source");

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_data_uri_src_get_caps);
  basesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_data_uri_src_get_size);
  basesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_data_uri_src_is_seekable);
  basesrc_class->create = GST_DEBUG_FUNCPTR (gst_data_uri_src_create);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_data_uri_src_start);
}

static void
gst_data_uri_src_init (GstDataURISrc * src)
{
}

static void
gst_data_uri_src_finalize (GObject * object)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (object);

  g_free (src->uri);
  src->uri = NULL;

  if (src->buffer)
    gst_buffer_unref (src->buffer);
  src->buffer = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_data_uri_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (object);

  switch (prop_id) {
    case PROP_URI:
      gst_data_uri_src_set_uri (GST_URI_HANDLER (src),
          g_value_get_string (value), NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_data_uri_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (object);

  switch (prop_id) {
    case PROP_URI:
      g_value_take_string (value,
          gst_data_uri_src_get_uri (GST_URI_HANDLER (src)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_data_uri_src_get_caps (GstBaseSrc * basesrc, GstCaps * filter)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (basesrc);
  GstCaps *caps;

  GST_OBJECT_LOCK (src);
  caps = gst_pad_get_current_caps (GST_BASE_SRC_PAD (basesrc));
  if (!caps)
    caps = gst_caps_new_any ();
  GST_OBJECT_UNLOCK (src);

  return caps;
}

static gboolean
gst_data_uri_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (basesrc);
  gboolean ret;

  GST_OBJECT_LOCK (src);
  if (!src->buffer) {
    ret = FALSE;
    *size = -1;
  } else {
    ret = TRUE;
    *size = gst_buffer_get_size (src->buffer);
  }
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_data_uri_src_is_seekable (GstBaseSrc * basesrc)
{
  return TRUE;
}

static GstFlowReturn
gst_data_uri_src_create (GstBaseSrc * basesrc, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (basesrc);
  GstFlowReturn ret;

  GST_OBJECT_LOCK (src);

  if (!src->buffer)
    goto no_buffer;

  /* This is only correct because GstBaseSrc already clips size for us to be no
   * larger than the max. available size if a segment at the end is requested */
  if (offset + size > gst_buffer_get_size (src->buffer)) {
    ret = GST_FLOW_EOS;
  } else if (*buf != NULL) {
    GstMapInfo src_info;
    GstMapInfo dest_info;
    gsize fill_size;

    gst_buffer_map (src->buffer, &src_info, GST_MAP_READ);
    gst_buffer_map (*buf, &dest_info, GST_MAP_WRITE);

    fill_size = gst_buffer_fill (*buf, 0, src_info.data + offset, size);

    gst_buffer_unmap (*buf, &dest_info);
    gst_buffer_unmap (src->buffer, &src_info);
    gst_buffer_set_size (*buf, fill_size);
    ret = GST_FLOW_OK;
  } else {
    *buf =
        gst_buffer_copy_region (src->buffer, GST_BUFFER_COPY_ALL, offset, size);
    ret = GST_FLOW_OK;
  }
  GST_OBJECT_UNLOCK (src);

  return ret;

/* ERRORS */
no_buffer:
  {
    GST_OBJECT_UNLOCK (src);
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL), (NULL));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_data_uri_src_start (GstBaseSrc * basesrc)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (basesrc);

  GST_OBJECT_LOCK (src);

  if (src->uri == NULL || *src->uri == '\0' || src->buffer == NULL)
    goto no_uri;

  GST_OBJECT_UNLOCK (src);

  return TRUE;

/* ERRORS */
no_uri:
  {
    GST_OBJECT_UNLOCK (src);
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("No valid data URI specified, or the data URI could not be parsed."),
        ("%s", src->uri));
    return FALSE;
  }
}

static GstURIType
gst_data_uri_src_get_uri_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_data_uri_src_get_protocols (GType type)
{
  static const gchar *protocols[] = { "data", 0 };

  return protocols;
}

static gchar *
gst_data_uri_src_get_uri (GstURIHandler * handler)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (handler);
  gchar *src_uri = NULL;

  GST_OBJECT_LOCK (src);
  src_uri = g_strdup (src->uri);
  GST_OBJECT_UNLOCK (src);
  return src_uri;
}

static gboolean
gst_data_uri_src_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstDataURISrc *src = GST_DATA_URI_SRC (handler);
  gboolean ret = FALSE;
  gchar *mimetype = NULL;
  const gchar *parameters_start;
  const gchar *data_start;
  const gchar *orig_uri = uri;
  GstCaps *caps;
  GstBuffer *buffer;
  gboolean base64 = FALSE;
  gchar *charset = NULL;
  gpointer bdata;
  gsize bsize;

  GST_OBJECT_LOCK (src);
  if (GST_STATE (src) >= GST_STATE_PAUSED)
    goto wrong_state;
  GST_OBJECT_UNLOCK (src);

  /* uri must be an URI as defined in RFC 2397
   * data:[<mediatype>][;base64],<data>
   */
  if (strncmp ("data:", uri, 5) != 0)
    goto invalid_uri;

  uri += 5;

  parameters_start = strchr (uri, ';');
  data_start = strchr (uri, ',');
  if (data_start == NULL)
    goto invalid_uri;

  if (data_start != uri && parameters_start != uri)
    mimetype =
        g_strndup (uri,
        (parameters_start ? parameters_start : data_start) - uri);
  else
    mimetype = g_strdup ("text/plain");

  GST_DEBUG_OBJECT (src, "Mimetype: %s", mimetype);

  if (parameters_start != NULL) {
    gchar **walk;
    gchar *parameters =
        g_strndup (parameters_start + 1, data_start - parameters_start - 1);
    gchar **parameters_strv;

    parameters_strv = g_strsplit (parameters, ";", -1);

    GST_DEBUG_OBJECT (src, "Parameters: ");
    walk = parameters_strv;
    while (*walk) {
      GST_DEBUG_OBJECT (src, "\t %s", *walk);
      if (strcmp ("base64", *walk) == 0) {
        base64 = TRUE;
      } else if (strncmp ("charset=", *walk, 8) == 0) {
        charset = g_strdup (*walk + 8);
      }
      walk++;
    }
    g_free (parameters);
    g_strfreev (parameters_strv);
  }

  /* Skip comma */
  data_start += 1;
  if (base64) {
    bdata = g_base64_decode (data_start, &bsize);
  } else {
    /* URI encoded, i.e. "percent" encoding */
    bdata = g_uri_unescape_string (data_start, NULL);
    if (bdata == NULL)
      goto invalid_uri_encoded_data;
    bsize = strlen (bdata) + 1;
  }
  /* Convert to UTF8 */
  if (strcmp ("text/plain", mimetype) == 0 &&
      charset && g_ascii_strcasecmp ("US-ASCII", charset) != 0
      && g_ascii_strcasecmp ("UTF-8", charset) != 0) {
    gsize read;
    gsize written;
    gpointer data;

    data =
        g_convert_with_fallback (bdata, -1, "UTF-8", charset, (char *) "*",
        &read, &written, NULL);
    g_free (bdata);

    bdata = data;
    bsize = written;
  }
  buffer = gst_buffer_new_wrapped (bdata, bsize);

  caps = gst_type_find_helper_for_buffer (GST_OBJECT (src), buffer, NULL);
  if (!caps)
    caps = gst_caps_new_empty_simple (mimetype);
  gst_base_src_set_caps (GST_BASE_SRC_CAST (src), caps);
  gst_caps_unref (caps);

  GST_OBJECT_LOCK (src);
  gst_buffer_replace (&src->buffer, buffer);
  gst_buffer_unref (buffer);
  g_free (src->uri);
  src->uri = g_strdup (orig_uri);
  GST_OBJECT_UNLOCK (src);

  ret = TRUE;

out:

  g_free (mimetype);
  g_free (charset);

  return ret;

wrong_state:
  {
    GST_WARNING_OBJECT (src, "Can't set URI in %s state",
        gst_element_state_get_name (GST_STATE (src)));
    GST_OBJECT_UNLOCK (src);
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_STATE,
        "Changing the 'uri' property on dataurisrc while it is running "
        "is not supported");
    goto out;
  }
invalid_uri:
  {
    GST_WARNING_OBJECT (src, "invalid URI '%s'", uri);
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid data URI");
    goto out;
  }
invalid_uri_encoded_data:
  {
    GST_WARNING_OBJECT (src, "Failed to parse data encoded in URI '%s'", uri);
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Could not parse data encoded in data URI");
    goto out;
  }
}

static void
gst_data_uri_src_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_data_uri_src_get_uri_type;
  iface->get_protocols = gst_data_uri_src_get_protocols;
  iface->get_uri = gst_data_uri_src_get_uri;
  iface->set_uri = gst_data_uri_src_set_uri;
}
