/* GStreamer bz2 decoder
 * Copyright (C) 2006 Lutz Müller <lutz topfrose de>

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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstbz2dec.h"

#include <bzlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (bz2dec_debug);
#define GST_CAT_DEFAULT bz2dec_debug

static const GstElementDetails bz2dec_details =
GST_ELEMENT_DETAILS ("BZ2 decoder",
    "Codec/Decoder", "Decodes compressed streams",
    "Lutz Mueller <lutz@users.sourceforge.net>");

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-bzip"));
static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

struct _GstBz2dec
{
  GstElement parent;

  /* Properties */
  guint first_buffer_size;
  guint buffer_size;

  gboolean ready;
  bz_stream stream;
  guint64 offset;
};

struct _GstBz2decClass
{
  GstElementClass parent_class;
};

GST_BOILERPLATE (GstBz2dec, gst_bz2dec, GstElement, GST_TYPE_ELEMENT);

#define DEFAULT_FIRST_BUFFER_SIZE 1024
#define DEFAULT_BUFFER_SIZE 1024

enum
{
  PROP_0,
  PROP_FIRST_BUFFER_SIZE,
  PROP_BUFFER_SIZE
};

static void
gst_bz2dec_decompress_end (GstBz2dec * b)
{
  g_return_if_fail (GST_IS_BZ2DEC (b));

  if (b->ready) {
    BZ2_bzDecompressEnd (&b->stream);
    memset (&b->stream, 0, sizeof (b->stream));
    b->ready = FALSE;
  }
}

static void
gst_bz2dec_decompress_init (GstBz2dec * b)
{
  g_return_if_fail (GST_IS_BZ2DEC (b));

  gst_bz2dec_decompress_end (b);
  b->offset = 0;
  switch (BZ2_bzDecompressInit (&b->stream, 0, 0)) {
    case BZ_OK:
      b->ready = TRUE;
      return;
    default:
      b->ready = FALSE;
      GST_ELEMENT_ERROR (b, CORE, FAILED, (NULL),
          ("Failed to start decompression."));
      return;
  }
}

typedef struct
{
  guint best_probability;
  GstCaps *caps;
  GstBuffer *buffer;
} SimpleTypeFind;

guint8 *
simple_find_peek (gpointer data, gint64 offset, guint size)
{
  SimpleTypeFind *find = (SimpleTypeFind *) data;

  if (offset < 0)
    return NULL;

  if (GST_BUFFER_SIZE (find->buffer) >= offset + size) {
    return GST_BUFFER_DATA (find->buffer) + offset;
  }
  return NULL;
}

static void
simple_find_suggest (gpointer data, guint probability, const GstCaps * caps)
{
  SimpleTypeFind *find = (SimpleTypeFind *) data;

  if (probability > find->best_probability) {
    GstCaps *copy = gst_caps_copy (caps);

    gst_caps_replace (&find->caps, copy);
    gst_caps_unref (copy);
    find->best_probability = probability;
  }
}

static GstCaps *
gst_bz2dec_do_typefind (GstBz2dec * b, GstBuffer * buffer)
{
  GList *walk, *type_list;
  SimpleTypeFind find;
  GstTypeFind gst_find;

  walk = type_list = gst_type_find_factory_get_list ();

  find.buffer = buffer;
  find.best_probability = 0;
  find.caps = NULL;
  gst_find.data = &find;
  gst_find.peek = simple_find_peek;
  gst_find.get_length = NULL;
  gst_find.suggest = simple_find_suggest;
  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      break;
    walk = g_list_next (walk);
  }
  gst_plugin_feature_list_free (type_list);
  if (find.best_probability > 0) {
    GST_DEBUG ("Found caps %s" GST_PTR_FORMAT " with buf size %u", find.caps,
        GST_BUFFER_SIZE (buffer));
    return find.caps;
  }

  return NULL;
}

static GstFlowReturn
gst_bz2dec_chain (GstPad * pad, GstBuffer * in)
{
  GstBz2dec *b = GST_BZ2DEC (gst_pad_get_parent (pad));
  GstPad *src = gst_element_get_pad (GST_ELEMENT (b), "src");
  int r = BZ_OK;

  gst_object_unref (b);
  gst_object_unref (src);
  if (!b->ready) {
    GST_ELEMENT_ERROR (b, CORE, FAILED, (NULL), ("Decompressor not ready."));
    return GST_FLOW_ERROR;
  }

  b->stream.next_in = (char *) GST_BUFFER_DATA (in);
  b->stream.avail_in = GST_BUFFER_SIZE (in);

  while (r != BZ_STREAM_END) {
    GstBuffer *out;
    GstCaps *caps;
    guint n;
    GstFlowReturn fr;

    /* Create the output buffer */
    if ((fr = gst_pad_alloc_buffer (src, b->offset,
                b->offset ? b->buffer_size : b->first_buffer_size,
                GST_PAD_CAPS (pad), &out)) != GST_FLOW_OK) {
      gst_bz2dec_decompress_init (b);
      return fr;
    }

    /* Decode */
    b->stream.next_out = (char *) GST_BUFFER_DATA (out);
    b->stream.avail_out = GST_BUFFER_SIZE (out);
    r = BZ2_bzDecompress (&b->stream);
    if ((r != BZ_OK) && (r != BZ_STREAM_END)) {
      GST_ELEMENT_ERROR (b, STREAM, DECODE, (NULL),
          ("Failed to decompress data (error code %i).", r));
      gst_bz2dec_decompress_init (b);
      gst_buffer_unref (out);
      return GST_FLOW_ERROR;
    }
    if (b->stream.avail_out >= GST_BUFFER_SIZE (out)) {
      gst_buffer_unref (out);
      break;
    }
    GST_BUFFER_SIZE (out) -= b->stream.avail_out;
    GST_BUFFER_OFFSET (out) = b->stream.total_out_lo32 - GST_BUFFER_SIZE (out);

    /* Configure source pad (if necessary) */
    if (!b->offset && (caps = gst_bz2dec_do_typefind (b, out))) {
      gst_buffer_set_caps (out, caps);
      gst_pad_set_caps (src, caps);
      gst_caps_unref (caps);
    }

    /* Push data */
    n = GST_BUFFER_SIZE (out);
    if ((fr = gst_pad_push (src, out)) != GST_FLOW_OK) {
      gst_buffer_unref (out);
      return fr;
    }
    b->offset += n;
  }
  gst_buffer_unref (in);
  return GST_FLOW_OK;
}

static void
gst_bz2dec_init (GstBz2dec * b, GstBz2decClass * klass)
{
  GstPad *pad;

  b->first_buffer_size = DEFAULT_FIRST_BUFFER_SIZE;
  b->buffer_size = DEFAULT_BUFFER_SIZE;
  pad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_chain_function (pad, gst_bz2dec_chain);
  gst_element_add_pad (GST_ELEMENT (b), pad);
  pad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (b), pad);

  gst_bz2dec_decompress_init (b);
}

static void
gst_bz2dec_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (ec,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (ec,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_details (ec, &bz2dec_details);
}

static void
gst_bz2dec_finalize (GObject * object)
{
  GstBz2dec *b = GST_BZ2DEC (object);

  gst_bz2dec_decompress_end (b);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_bz2dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBz2dec *b = GST_BZ2DEC (object);

  switch (prop_id) {
    case PROP_BUFFER_SIZE:
      g_value_set_uint (value, b->buffer_size);
      break;
    case PROP_FIRST_BUFFER_SIZE:
      g_value_set_uint (value, b->first_buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_bz2dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBz2dec *b = GST_BZ2DEC (object);

  switch (prop_id) {
    case PROP_BUFFER_SIZE:
      b->buffer_size = g_value_get_uint (value);
      break;
    case PROP_FIRST_BUFFER_SIZE:
      b->first_buffer_size = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_bz2dec_class_init (GstBz2decClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_bz2dec_finalize;
  gobject_class->get_property = gst_bz2dec_get_property;
  gobject_class->set_property = gst_bz2dec_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_FIRST_BUFFER_SIZE, g_param_spec_uint ("first_buffer_size",
          "Size of first buffer", "Size of first buffer (used to determine the "
          "mime type of the uncompressed data)", 1, G_MAXUINT,
          DEFAULT_FIRST_BUFFER_SIZE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer_size", "Buffer size", "Buffer size",
          1, G_MAXUINT, DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (bz2dec_debug, "bz2dec", 0, "BZ2 decompressor");
}
