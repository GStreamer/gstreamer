/* GStreamer bz2 encoder
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
#include "gstbz2enc.h"

#include <bzlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (bz2enc_debug);
#define GST_CAT_DEFAULT bz2enc_debug

static const GstElementDetails bz2enc_details =
GST_ELEMENT_DETAILS ("BZ2 encoder",
    "Codec/Encoder", "Compresses streams",
    "Lutz Mueller <lutz@users.sourceforge.net>");

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));
static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-bzip"));

#define DEFAULT_BLOCK_SIZE 6
#define DEFAULT_BUFFER_SIZE 1024

enum
{
  PROP_0,
  PROP_BLOCK_SIZE,
  PROP_BUFFER_SIZE
};

struct _GstBz2enc
{
  GstElement parent;

  /* Properties */
  guint block_size;
  guint buffer_size;

  gboolean ready;
  bz_stream stream;
  guint64 offset;
};

struct _GstBz2encClass
{
  GstElementClass parent_class;
};

GST_BOILERPLATE (GstBz2enc, gst_bz2enc, GstElement, GST_TYPE_ELEMENT);

static void
gst_bz2enc_compress_end (GstBz2enc * b)
{
  g_return_if_fail (GST_IS_BZ2ENC (b));

  if (b->ready) {
    BZ2_bzCompressEnd (&b->stream);
    memset (&b->stream, 0, sizeof (b->stream));
    b->ready = FALSE;
  }
}

static void
gst_bz2enc_compress_init (GstBz2enc * b)
{
  g_return_if_fail (GST_IS_BZ2ENC (b));

  gst_bz2enc_compress_end (b);
  b->offset = 0;
  switch (BZ2_bzCompressInit (&b->stream, b->block_size, 0, 0)) {
    case BZ_OK:
      b->ready = TRUE;
      return;
    default:
      b->ready = FALSE;
      GST_ELEMENT_ERROR (b, CORE, FAILED, (NULL),
          ("Failed to start compression."));
      return;
  }
}

static gboolean
gst_bz2enc_event (GstPad * pad, GstEvent * e)
{
  GstBz2enc *b = GST_BZ2ENC (gst_pad_get_parent (pad));
  GstPad *src = gst_element_get_pad (GST_ELEMENT (b), "src");
  int r = BZ_FINISH_OK;
  GstFlowReturn fr;

  gst_object_unref (b);
  gst_object_unref (src);
  switch (GST_EVENT_TYPE (e)) {
    case GST_EVENT_EOS:
      while (r != BZ_STREAM_END) {
        GstBuffer *out;

        if ((fr = gst_pad_alloc_buffer (src, b->offset, b->buffer_size,
                    GST_PAD_CAPS (src), &out)) != GST_FLOW_OK) {
          GST_ELEMENT_ERROR (b, STREAM, DECODE, (NULL),
              ("Failed to allocate buffer of size %i.", b->buffer_size));
          break;
        }
        b->stream.next_out = (char *) GST_BUFFER_DATA (out);
        b->stream.avail_out = GST_BUFFER_SIZE (out);
        r = BZ2_bzCompress (&b->stream, BZ_FINISH);
        if ((r != BZ_FINISH_OK) && (r != BZ_STREAM_END)) {
          GST_ELEMENT_ERROR (b, STREAM, DECODE, (NULL),
              ("Failed to finish to compress (error code %i).", r));
          gst_buffer_unref (out);
          break;
        }
        if (b->stream.avail_out >= GST_BUFFER_SIZE (out)) {
          gst_buffer_unref (out);
          break;
        }
        GST_BUFFER_SIZE (out) -= b->stream.avail_out;
        GST_BUFFER_OFFSET (out) =
            b->stream.total_out_lo32 - GST_BUFFER_SIZE (out);
        if ((fr = gst_pad_push (src, out)) != GST_FLOW_OK) {
          GST_ELEMENT_ERROR (b, STREAM, DECODE, (NULL),
              ("Could not push last packet (error code %i).", r));
          gst_buffer_unref (out);
          break;
        }
      }
      gst_bz2enc_compress_init (b);
      break;
    default:
      break;
  }
  return gst_pad_event_default (pad, e);
}

static GstFlowReturn
gst_bz2enc_chain (GstPad * pad, GstBuffer * in)
{
  GstBz2enc *b = GST_BZ2ENC (gst_pad_get_parent (pad));
  GstPad *src = gst_element_get_pad (GST_ELEMENT (b), "src");
  GstFlowReturn fr = GST_FLOW_OK;
  guint n;

  gst_object_unref (b);
  gst_object_unref (src);
  if (!b->ready) {
    GST_ELEMENT_ERROR (b, CORE, FAILED, (NULL), ("Compressor not ready."));
    return GST_FLOW_ERROR;
  }

  b->stream.next_in = (char *) GST_BUFFER_DATA (in);
  b->stream.avail_in = GST_BUFFER_SIZE (in);
  while ((fr == GST_FLOW_OK) && b->stream.avail_in) {
    GstBuffer *out;
    int r;

    if ((fr = gst_pad_alloc_buffer (src, b->offset, b->buffer_size,
                GST_PAD_CAPS (pad), &out)) != GST_FLOW_OK) {
      gst_bz2enc_compress_init (b);
      return fr;
    }
    b->stream.next_out = (char *) GST_BUFFER_DATA (out);
    b->stream.avail_out = GST_BUFFER_SIZE (out);
    r = BZ2_bzCompress (&b->stream, BZ_RUN);
    if (r != BZ_RUN_OK) {
      GST_ELEMENT_ERROR (b, STREAM, DECODE, (NULL),
          ("Failed to compress data (error code %i).", r));
      gst_bz2enc_compress_init (b);
      gst_buffer_unref (out);
      return GST_FLOW_ERROR;
    }
    if (b->stream.avail_out >= GST_BUFFER_SIZE (out)) {
      gst_buffer_unref (out);
      break;
    }
    GST_BUFFER_SIZE (out) -= b->stream.avail_out;
    GST_BUFFER_OFFSET (out) = b->stream.total_out_lo32 - GST_BUFFER_SIZE (out);
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
gst_bz2enc_init (GstBz2enc * b, GstBz2encClass * klass)
{
  GstPad *pad;

  pad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (pad, gst_bz2enc_chain);
  gst_pad_set_event_function (pad, gst_bz2enc_event);
  gst_element_add_pad (GST_ELEMENT (b), pad);
  pad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (b), pad);

  b->block_size = DEFAULT_BLOCK_SIZE;
  b->buffer_size = DEFAULT_BUFFER_SIZE;
  gst_bz2enc_compress_init (b);
}

static void
gst_bz2enc_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (ec,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (ec,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_details (ec, &bz2enc_details);
}

static void
gst_bz2enc_finalize (GObject * object)
{
  GstBz2enc *b = GST_BZ2ENC (object);

  gst_bz2enc_compress_end (b);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_bz2enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBz2enc *b = GST_BZ2ENC (object);

  switch (prop_id) {
    case PROP_BLOCK_SIZE:
      g_value_set_uint (value, b->block_size);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_uint (value, b->buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_bz2enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBz2enc *b = GST_BZ2ENC (object);

  switch (prop_id) {
    case PROP_BLOCK_SIZE:
      b->block_size = g_value_get_uint (value);
      gst_bz2enc_compress_init (b);
      break;
    case PROP_BUFFER_SIZE:
      b->buffer_size = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_bz2enc_class_init (GstBz2encClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_bz2enc_finalize;
  gobject_class->set_property = gst_bz2enc_set_property;
  gobject_class->get_property = gst_bz2enc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCK_SIZE,
      g_param_spec_uint ("block_size", "Block size", "Block size",
          1, 9, DEFAULT_BLOCK_SIZE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer_size", "Buffer size", "Buffer size",
          1, G_MAXUINT, DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (bz2enc_debug, "bz2enc", 0, "BZ2 compressor");
}
