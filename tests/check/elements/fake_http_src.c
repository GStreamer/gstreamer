/* GStreamer unit test for MPEG-DASH
 *
 * Copyright (c) <2015> YouView TV Ltd
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

#include <gst/check/gstcheck.h>
#include <gst/base/gstbasesrc.h>

#include "fake_http_src.h"

#ifndef GST_PACKAGE_NAME
#define GST_PACKAGE_NAME "gst-plugins-bad"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "https://developer.gnome.org/gstreamer/"
#endif

#define GST_FAKE_SOUP_HTTP_SRC(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FAKE_SOUP_HTTP_SRC, GstFakeSoupHTTPSrc))
#define GST_FAKE_SOUP_HTTP_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FAKE_SOUP_HTTP_SRC, GstFakeSoupHTTPSrcClass))
#define GST_IS_FAKE_SOUP_HTTP_SRC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FAKE_SOUP_HTTP_SRC))
#define GST_IS_FAKE_SOUP_HTTP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FAKE_SOUP_HTTP_SRC))
#define GST_FAKE_SOUP_HTTP_SRC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_FAKE_SOUP_HTTP_SRC, GstFakeSoupHTTPSrcClass))

#define GST_FAKE_SOUP_HTTP_SRC_GET_LOCK(d)  (&(((GstFakeSoupHTTPSrc*)(d))->lock))
#define GST_FAKE_SOUP_HTTP_SRC_LOCK(d)      g_mutex_lock (GST_FAKE_SOUP_HTTP_SRC_GET_LOCK (d))
#define GST_FAKE_SOUP_HTTP_SRC_UNLOCK(d)    g_mutex_unlock (GST_FAKE_SOUP_HTTP_SRC_GET_LOCK (d))

typedef struct _GstFakeSoupHTTPSrc
{
  GstBaseSrc parent;

  /* uri for which to retrieve data */
  gchar *uri;
  /* data to retrieve.
   * If NULL, we will fake a buffer of size bytes, containing numbers in sequence
   * 0, 4, 8, ...
   * Each number is written on sizeof(int) bytes in little endian format
   */
  const gchar *payload;
  /* size of data to generate */
  guint size;
  /* position from where to retrieve data */
  guint64 position;
  /* index immediately after the last byte from the segment to be retrieved */
  guint64 segment_end;

  /* download error code to simulate during create function */
  guint downloadErrorCode;

  /* mutex to protect multithread access to this structure */
  GMutex lock;
} GstFakeSoupHTTPSrc;

typedef struct _GstFakeSoupHTTPSrcClass
{
  GstBaseSrcClass parent_class;
} GstFakeSoupHTTPSrcClass;

typedef struct _PluginInitContext
{
  const gchar *name;
  guint rank;
  GType type;
} PluginInitContext;


static const GstFakeHttpSrcInputData *gst_fake_soup_http_src_inputData = NULL;

static GstStaticPadTemplate gst_dashdemux_test_source_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_fake_soup_http_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_fake_soup_http_src_finalize (GObject * object);
static gboolean gst_fake_soup_http_src_is_seekable (GstBaseSrc * basesrc);
static gboolean gst_fake_soup_http_src_do_seek (GstBaseSrc * basesrc,
    GstSegment * segment);
static gboolean gst_fake_soup_http_src_start (GstBaseSrc * basesrc);
static gboolean gst_fake_soup_http_src_stop (GstBaseSrc * basesrc);
static gboolean gst_fake_soup_http_src_get_size (GstBaseSrc * basesrc,
    guint64 * size);
static GstFlowReturn gst_fake_soup_http_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint length, GstBuffer ** ret);

#define _do_init \
  G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_fake_soup_http_src_uri_handler_init);

#define gst_fake_soup_http_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstFakeSoupHTTPSrc, gst_fake_soup_http_src,
    GST_TYPE_BASE_SRC, _do_init);

static void
gst_fake_soup_http_src_class_init (GstFakeSoupHTTPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->finalize = gst_fake_soup_http_src_finalize;

  gst_element_class_set_metadata (gstelement_class,
      "Fake HTTP source element for unit tests",
      "Source/Network",
      "Use in unit tests", "Alex Ashley <alex.ashley@youview.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_dashdemux_test_source_template));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_fake_soup_http_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_fake_soup_http_src_stop);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_fake_soup_http_src_is_seekable);
  gstbasesrc_class->do_seek =
      GST_DEBUG_FUNCPTR (gst_fake_soup_http_src_do_seek);
  gstbasesrc_class->get_size =
      GST_DEBUG_FUNCPTR (gst_fake_soup_http_src_get_size);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_fake_soup_http_src_create);

}

static void
gst_fake_soup_http_src_init (GstFakeSoupHTTPSrc * src)
{
  src->uri = NULL;
  src->payload = NULL;
  src->position = 0;
  src->size = 0;
  src->segment_end = 0;
  src->downloadErrorCode = 0;
  gst_base_src_set_blocksize (GST_BASE_SRC (src),
      GST_FAKE_SOUP_HTTP_SRC_MAX_BUF_SIZE);
  g_mutex_init (&src->lock);
}

static void
gst_fake_soup_http_src_finalize (GObject * object)
{
  GstFakeSoupHTTPSrc *src;

  src = GST_FAKE_SOUP_HTTP_SRC (object);

  g_mutex_clear (&src->lock);
  g_free (src->uri);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_fake_soup_http_src_start (GstBaseSrc * basesrc)
{
  GstFakeSoupHTTPSrc *src;
  const GstFakeHttpSrcInputData *input = gst_fake_soup_http_src_inputData;

  src = GST_FAKE_SOUP_HTTP_SRC (basesrc);

  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);

  if (!src->uri) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (("No URL set.")),
        ("Missing location property"));
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return FALSE;
  }

  for (guint i = 0; input[i].uri; ++i) {
    if (strcmp (input[i].uri, src->uri) == 0) {
      src->payload = input[i].payload;
      src->position = 0;
      if (src->payload)
        src->size = strlen (src->payload);
      else
        src->size = input[i].size;
      src->segment_end = src->size;
      src->downloadErrorCode = 0;
      gst_base_src_set_dynamic_size (basesrc, FALSE);
      GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
      return TRUE;
    }
  }

  GST_WARNING
      ("gst_fake_soup_http_src_start cannot find url '%s' in input data",
      src->uri);
  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
  return FALSE;
}

static gboolean
gst_fake_soup_http_src_stop (GstBaseSrc * basesrc)
{
  GstFakeSoupHTTPSrc *src;

  src = GST_FAKE_SOUP_HTTP_SRC (basesrc);
  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);

  src->payload = NULL;
  src->position = 0;
  src->size = 0;

  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
  return TRUE;
}

static gboolean
gst_fake_soup_http_src_is_seekable (GstBaseSrc * basesrc)
{
  GstFakeSoupHTTPSrc *src;
  gboolean ret;

  src = GST_FAKE_SOUP_HTTP_SRC (basesrc);

  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);

  /* if size is set, we can seek */
  ret = src->size > 0;

  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);

  return ret;
}

static gboolean
gst_fake_soup_http_src_do_seek (GstBaseSrc * basesrc, GstSegment * segment)
{
  GstFakeSoupHTTPSrc *src;

  GST_DEBUG ("gst_fake_soup_http_src_do_seek start = %" G_GUINT64_FORMAT,
      segment->start);
  src = GST_FAKE_SOUP_HTTP_SRC (basesrc);

  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);

  /*
     According to RFC7233, the range is inclusive:
     The first-byte-pos value in a byte-range-spec gives the byte-offset
     of the first byte in a range.  The last-byte-pos value gives the
     byte-offset of the last byte in the range; that is, the byte
     positions specified are inclusive.  Byte offsets start at zero.
   */

  if (!src->uri) {
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return FALSE;
  }

  if (segment->start >= src->size) {
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return FALSE;
  }

  if (segment->stop != -1 && segment->stop > src->size) {
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return FALSE;
  }

  src->position = segment->start;

  if (segment->stop != -1) {
    src->segment_end = segment->stop;
  }

  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
  return TRUE;
}

static gboolean
gst_fake_soup_http_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstFakeSoupHTTPSrc *src;
  const GstFakeHttpSrcInputData *input = gst_fake_soup_http_src_inputData;

  src = GST_FAKE_SOUP_HTTP_SRC (basesrc);

  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);

  if (!src->uri) {
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return FALSE;
  }

  /* if it was started (payload or size configured), size is set */
  if (src->payload || src->size > 0) {
    *size = src->size;
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return TRUE;
  }

  /* it wasn't started, compute the size */
  for (guint i = 0; input[i].uri; ++i) {
    if (strcmp (input[i].uri, src->uri) == 0) {
      if (input[i].payload)
        *size = strlen (input[i].payload);
      else
        *size = input[i].size;
      GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
      return TRUE;
    }
  }
  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
  return FALSE;
}

static GstFlowReturn
gst_fake_soup_http_src_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** ret)
{
  GstFakeSoupHTTPSrc *src;
  guint bytes_read;
  GstBuffer *buf;

  src = GST_FAKE_SOUP_HTTP_SRC (basesrc);

  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);

  GST_DEBUG ("gst_fake_soup_http_src_create feeding from %" G_GUINT64_FORMAT,
      src->position);
  if (src->uri == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return GST_FLOW_ERROR;
  }
  if (src->downloadErrorCode) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, ("%s",
            "Generated requested error"), ("%s (%d), URL: %s, Redirect to: %s",
            "Generated requested error", src->downloadErrorCode, src->uri,
            GST_STR_NULL (NULL)));
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return GST_FLOW_ERROR;
  }

  bytes_read = MIN ((src->segment_end - src->position),
      GST_FAKE_SOUP_HTTP_SRC_MAX_BUF_SIZE);
  if (bytes_read == 0) {
    GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
    return GST_FLOW_EOS;
  }

  buf = gst_buffer_new_allocate (NULL, bytes_read, NULL);
  fail_if (buf == NULL, "Not enough memory to allocate buffer");

  if (src->payload) {
    gst_buffer_fill (buf, 0, src->payload + src->position, bytes_read);
  } else {
    GstMapInfo info;
    guint pattern;

    pattern = src->position - src->position % sizeof (pattern);

    gst_buffer_map (buf, &info, GST_MAP_WRITE);
    for (guint64 i = 0; i < bytes_read; ++i) {
      gchar pattern_byte_to_write = (src->position + i) % sizeof (pattern);
      if (pattern_byte_to_write == 0) {
        pattern = src->position + i;
      }
      info.data[i] = (pattern >> (pattern_byte_to_write * 8)) & 0xFF;
    }
    gst_buffer_unmap (buf, &info);
  }

  GST_BUFFER_OFFSET (buf) = src->position;
  GST_BUFFER_OFFSET_END (buf) = src->position + bytes_read;
  *ret = buf;

  src->position += bytes_read;

  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
  return GST_FLOW_OK;
}

/* must be called with the lock taken */
static gboolean
gst_fake_soup_http_src_set_location (GstFakeSoupHTTPSrc * src,
    const gchar * uri, GError ** error)
{
  g_free (src->uri);
  src->uri = g_strdup (uri);
  return TRUE;
}

static GstURIType
gst_fake_soup_http_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_fake_soup_http_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "http", NULL };

  return protocols;
}

static gchar *
gst_fake_soup_http_src_uri_get_uri (GstURIHandler * handler)
{
  GstFakeSoupHTTPSrc *src = GST_FAKE_SOUP_HTTP_SRC (handler);
  gchar *uri;

  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);
  uri = g_strdup (src->uri);
  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);
  return uri;
}

static gboolean
gst_fake_soup_http_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** err)
{
  GstFakeSoupHTTPSrc *src = GST_FAKE_SOUP_HTTP_SRC (handler);
  gboolean ret;

  GST_FAKE_SOUP_HTTP_SRC_LOCK (src);

  ret = gst_fake_soup_http_src_set_location (src, uri, err);

  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (src);

  return ret;
}

static void
gst_fake_soup_http_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_fake_soup_http_src_uri_get_type;
  iface->get_protocols = gst_fake_soup_http_src_uri_get_protocols;
  iface->get_uri = gst_fake_soup_http_src_uri_get_uri;
  iface->set_uri = gst_fake_soup_http_src_uri_set_uri;
}

static gboolean
gst_fake_soup_http_src_plugin_init_func (GstPlugin * plugin, gpointer user_data)
{
  PluginInitContext *context = (PluginInitContext *) user_data;
  gboolean ret;

  ret =
      gst_element_register (plugin, context->name, context->rank,
      context->type);
  return ret;
}

gboolean
gst_fake_soup_http_src_register_plugin (GstRegistry * registry,
    const gchar * name)
{
  gboolean ret;
  PluginInitContext context;

  context.name = name;
  context.rank = GST_RANK_PRIMARY + 1;
  context.type = GST_TYPE_FAKE_SOUP_HTTP_SRC;
  ret = gst_plugin_register_static_full (GST_VERSION_MAJOR,     /* version */
      GST_VERSION_MINOR,        /* version */
      name,                     /* name */
      "Replaces a souphttpsrc plugin and returns predefined data.",     /* description */
      gst_fake_soup_http_src_plugin_init_func,  /* init function */
      "0.0.0",                  /* version string */
      GST_LICENSE_UNKNOWN,      /* license */
      __FILE__,                 /* source */
      GST_PACKAGE_NAME,         /* package */
      GST_PACKAGE_ORIGIN,       /* origin */
      &context                  /* user_data */
      );
  return ret;
}

/**
 * gst_fake_soup_http_src_set_input_data:
 * @input: array of #GstFakeHttpSrcInputData that is used when
 * responding to a request. The last entry in the array must
 * have the uri field set to NULL
 */
void
gst_fake_soup_http_src_set_input_data (const GstFakeHttpSrcInputData * input)
{
  gst_fake_soup_http_src_inputData = input;
}

void
gst_fake_soup_http_src_simulate_download_error (GstFakeSoupHTTPSrc *
    fakeSoupHTTPSrc, guint downloadErrorCode)
{
  GST_FAKE_SOUP_HTTP_SRC_LOCK (fakeSoupHTTPSrc);
  fakeSoupHTTPSrc->downloadErrorCode = downloadErrorCode;
  GST_FAKE_SOUP_HTTP_SRC_UNLOCK (fakeSoupHTTPSrc);
}
