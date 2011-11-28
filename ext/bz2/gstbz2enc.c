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

  GstPad *sink;
  GstPad *src;

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
  GstBz2enc *b;
  gboolean ret;

  b = GST_BZ2ENC (gst_pad_get_parent (pad));
  switch (GST_EVENT_TYPE (e)) {
    case GST_EVENT_EOS:{
      GstFlowReturn flow;
      int r = BZ_FINISH_OK;

      do {
        GstBuffer *out;

        flow = gst_pad_alloc_buffer (b->src, b->offset, b->buffer_size,
            GST_PAD_CAPS (b->src), &out);

        if (flow != GST_FLOW_OK) {
          GST_DEBUG_OBJECT (b, "pad alloc on EOS failed: %s",
              gst_flow_get_name (flow));
          break;
        }

        b->stream.next_out = (char *) GST_BUFFER_DATA (out);
        b->stream.avail_out = GST_BUFFER_SIZE (out);
        r = BZ2_bzCompress (&b->stream, BZ_FINISH);
        if ((r != BZ_FINISH_OK) && (r != BZ_STREAM_END)) {
          GST_ELEMENT_ERROR (b, STREAM, ENCODE, (NULL),
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

        flow = gst_pad_push (b->src, out);

        if (flow != GST_FLOW_OK) {
          GST_DEBUG_OBJECT (b, "push on EOS failed: %s",
              gst_flow_get_name (flow));
          break;
        }
      } while (r != BZ_STREAM_END);

      ret = gst_pad_event_default (pad, e);

      if (r != BZ_STREAM_END || flow != GST_FLOW_OK)
        ret = FALSE;

      gst_bz2enc_compress_init (b);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, e);
      break;
  }

  gst_object_unref (b);

  return ret;
}

static GstFlowReturn
gst_bz2enc_chain (GstPad * pad, GstBuffer * in)
{
  GstFlowReturn flow = GST_FLOW_OK;
  GstBuffer *out;
  GstBz2enc *b;
  guint n;
  int bz2_ret;

  b = GST_BZ2ENC (GST_PAD_PARENT (pad));

  if (!b->ready)
    goto not_ready;

  b->stream.next_in = (char *) GST_BUFFER_DATA (in);
  b->stream.avail_in = GST_BUFFER_SIZE (in);
  while (b->stream.avail_in) {
    flow = gst_pad_alloc_buffer (b->src, b->offset, b->buffer_size,
        GST_PAD_CAPS (pad), &out);

    if (flow != GST_FLOW_OK) {
      gst_bz2enc_compress_init (b);
      break;
    }

    b->stream.next_out = (char *) GST_BUFFER_DATA (out);
    b->stream.avail_out = GST_BUFFER_SIZE (out);
    bz2_ret = BZ2_bzCompress (&b->stream, BZ_RUN);
    if (bz2_ret != BZ_RUN_OK)
      goto compress_error;

    if (b->stream.avail_out >= GST_BUFFER_SIZE (out)) {
      gst_buffer_unref (out);
      break;
    }

    GST_BUFFER_SIZE (out) -= b->stream.avail_out;
    GST_BUFFER_OFFSET (out) = b->stream.total_out_lo32 - GST_BUFFER_SIZE (out);
    n = GST_BUFFER_SIZE (out);

    flow = gst_pad_push (b->src, out);

    if (flow != GST_FLOW_OK)
      break;

    b->offset += n;
  }

done:

  gst_buffer_unref (in);
  return flow;

/* ERRORS */
not_ready:
  {
    GST_ELEMENT_ERROR (b, LIBRARY, FAILED, (NULL), ("Compressor not ready."));
    flow = GST_FLOW_WRONG_STATE;
    goto done;
  }
compress_error:
  {
    GST_ELEMENT_ERROR (b, STREAM, ENCODE, (NULL),
        ("Failed to compress data (error code %i)", bz2_ret));
    gst_bz2enc_compress_init (b);
    gst_buffer_unref (out);
    flow = GST_FLOW_ERROR;
    goto done;
  }
}

static void
gst_bz2enc_init (GstBz2enc * b, GstBz2encClass * klass)
{
  b->sink = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (b->sink, GST_DEBUG_FUNCPTR (gst_bz2enc_chain));
  gst_pad_set_event_function (b->sink, GST_DEBUG_FUNCPTR (gst_bz2enc_event));
  gst_element_add_pad (GST_ELEMENT (b), b->sink);

  b->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_caps (b->src, gst_static_pad_template_get_caps (&src_template));
  gst_pad_use_fixed_caps (b->src);
  gst_element_add_pad (GST_ELEMENT (b), b->src);

  b->block_size = DEFAULT_BLOCK_SIZE;
  b->buffer_size = DEFAULT_BUFFER_SIZE;
  gst_bz2enc_compress_init (b);
}

static void
gst_bz2enc_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (ec, &sink_template);
  gst_element_class_add_static_pad_template (ec, &src_template);
  gst_element_class_set_details_simple (ec, "BZ2 encoder",
      "Codec/Encoder", "Compresses streams",
      "Lutz Mueller <lutz@users.sourceforge.net>");
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
      g_param_spec_uint ("block-size", "Block size", "Block size",
          1, 9, DEFAULT_BLOCK_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer-size", "Buffer size", "Buffer size",
          1, G_MAXUINT, DEFAULT_BUFFER_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (bz2enc_debug, "bz2enc", 0, "BZ2 compressor");
}
