/* GStreamer Musepack decoder plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmusepackdec.h"
#include "gstmusepackreader.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-musepack")
    );

#ifdef MPC_FIXED_POINT
#define BASE_CAPS \
  "audio/x-raw-int, " \
    "signed = (bool) TRUE, " \
    "width = (int) 32, " \
    "depth = (int) 32"
#else
#define BASE_CAPS \
  "audio/x-raw-float, " \
    "width = (int) 32, " \
    "buffer-frames = (int) 0"
#endif

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (BASE_CAPS ", "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );

static void gst_musepackdec_base_init (GstMusepackDecClass * klass);
static void gst_musepackdec_class_init (GstMusepackDecClass * klass);
static void gst_musepackdec_init (GstMusepackDec * musepackdec);
static void gst_musepackdec_dispose (GObject * obj);

static gboolean gst_musepackdec_src_event (GstPad * pad, GstEvent * event);
static const GstFormat *gst_musepackdec_get_formats (GstPad * pad);
static const GstEventMask *gst_musepackdec_get_event_masks (GstPad * pad);
static const GstQueryType *gst_musepackdec_get_query_types (GstPad * pad);
static gboolean gst_musepackdec_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);
static gboolean gst_musepackdec_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static void gst_musepackdec_loop (GstElement * element);
static GstStateChangeReturn
gst_musepackdec_change_state (GstElement * element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

/* static guint gst_musepackdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_musepackdec_get_type (void)
{
  static GType gst_musepackdec_type = 0;

  if (!gst_musepackdec_type) {
    static const GTypeInfo gst_musepackdec_info = {
      sizeof (GstMusepackDecClass),
      (GBaseInitFunc) gst_musepackdec_base_init,
      NULL,
      (GClassInitFunc) gst_musepackdec_class_init,
      NULL,
      NULL,
      sizeof (GstMusepackDec),
      0,
      (GInstanceInitFunc) gst_musepackdec_init,
    };

    gst_musepackdec_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstMusepackDec", &gst_musepackdec_info, (GTypeFlags) 0);
  }

  return gst_musepackdec_type;
}

static void
gst_musepackdec_base_init (GstMusepackDecClass * klass)
{
  static GstElementDetails gst_musepackdec_details =
      GST_ELEMENT_DETAILS ("Musepack decoder",
      "Codec/Decoder/Audio",
      "Musepack decoder",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_musepackdec_details);
}

static void
gst_musepackdec_class_init (GstMusepackDecClass * klass)
{
  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  GST_ELEMENT_CLASS (klass)->change_state = gst_musepackdec_change_state;
  G_OBJECT_CLASS (klass)->dispose = gst_musepackdec_dispose;
}

static void
gst_musepackdec_init (GstMusepackDec * musepackdec)
{
  GST_FLAG_SET (musepackdec, GST_ELEMENT_EVENT_AWARE);

  musepackdec->r = g_new (mpc_reader, 1);
  musepackdec->d = g_new (mpc_decoder, 1);
  musepackdec->init = FALSE;
  musepackdec->seek_pending = FALSE;

  musepackdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (musepackdec), musepackdec->sinkpad);

  musepackdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_set_event_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_src_event));
  gst_pad_set_event_mask_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_get_event_masks));
  gst_pad_set_query_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_src_query));
  gst_pad_set_query_type_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_get_query_types));
  gst_pad_set_convert_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_src_convert));
  gst_pad_set_formats_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_get_formats));
  gst_pad_use_explicit_caps (musepackdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (musepackdec), musepackdec->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (musepackdec),
      gst_musepackdec_loop);
}

static void
gst_musepackdec_dispose (GObject * obj)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (obj);

  g_free (musepackdec->r);
  musepackdec->r = NULL;
  g_free (musepackdec->d);
  musepackdec->d = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static gboolean
gst_musepackdec_src_event (GstPad * pad, GstEvent * event)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      gint64 offset, len, pos;
      GstFormat fmt = GST_FORMAT_TIME;

      /* in time */
      if (!gst_pad_convert (pad,
              (GstFormat) GST_EVENT_SEEK_FORMAT (event),
              GST_EVENT_SEEK_OFFSET (event),
              &fmt, &offset) ||
          !gst_pad_convert (pad,
              GST_FORMAT_DEFAULT, musepackdec->len,
              &fmt, &len) ||
          !gst_pad_convert (pad,
              GST_FORMAT_DEFAULT, musepackdec->pos, &fmt, &pos)) {
        res = FALSE;
        break;
      }

      /* offset from start */
      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_SET:
          break;
        case GST_SEEK_METHOD_CUR:
          offset += pos;
          break;
        case GST_SEEK_METHOD_END:
          offset = len - offset;
          break;
        default:
          res = FALSE;
          goto done;
      }

      /* only valid seeks */
      if (offset >= len || offset < 0) {
        res = FALSE;
        break;
      }

      /* store */
      musepackdec->seek_pending = TRUE;
      musepackdec->flush_pending =
          GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;
      musepackdec->seek_time = offset;
      res = TRUE;
      break;
    }
    default:
      res = FALSE;
      break;
  }

done:
  gst_event_unref (event);

  return res;
}

static const GstFormat *
gst_musepackdec_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    GST_FORMAT_TIME,
    (GstFormat) 0
  };

  return formats;
}

static const GstEventMask *
gst_musepackdec_get_event_masks (GstPad * pad)
{
  static const GstEventMask event_masks[] = {
    {GST_EVENT_SEEK,
        (GstEventFlag) (GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH)},
    {(GstEventType) 0, (GstEventFlag) 0}
  };

  return event_masks;
}

static const GstQueryType *
gst_musepackdec_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    (GstQueryType) 0
  };

  return query_types;
}

static gboolean
gst_musepackdec_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  gboolean res;

  if (!musepackdec->init)
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      res = gst_pad_convert (pad,
          GST_FORMAT_DEFAULT, musepackdec->len, format, value);
      break;
    case GST_QUERY_POSITION:
      res = gst_pad_convert (pad,
          GST_FORMAT_DEFAULT, musepackdec->pos, format, value);
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static gboolean
gst_musepackdec_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  if (!musepackdec->init)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / musepackdec->rate;
          break;
        case GST_FORMAT_BYTES:
          *dest_value = src_value * musepackdec->bps;
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * musepackdec->rate / GST_SECOND;
          break;
        case GST_FORMAT_BYTES:
          *dest_value = src_value * musepackdec->rate *
              musepackdec->bps / GST_SECOND;
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / musepackdec->bps;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND /
              (musepackdec->bps * musepackdec->rate);
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    default:
      res = FALSE;
      break;
  }

  return TRUE;
}

static gboolean
gst_musepack_stream_init (GstMusepackDec * musepackdec)
{
  mpc_streaminfo i;
  GstCaps *caps;

  /* set up reading */
  gst_musepack_init_reader (musepackdec->r, musepackdec->bs);

  /* streaminfo */
  mpc_streaminfo_init (&i);
  if (mpc_streaminfo_read (&i, musepackdec->r) < 0) {
    GST_ELEMENT_ERROR (musepackdec, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }

  /* decoding */
  mpc_decoder_setup (musepackdec->d, musepackdec->r);
  mpc_decoder_scale_output (musepackdec->d, 1.0);
  if (!mpc_decoder_initialize (musepackdec->d, &i)) {
    GST_ELEMENT_ERROR (musepackdec, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }

  /* capsnego */
  caps = gst_caps_from_string (BASE_CAPS);
  gst_caps_set_simple (caps,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "channels", G_TYPE_INT, i.channels,
      "rate", G_TYPE_INT, i.sample_freq, NULL);
  if (!gst_pad_set_explicit_caps (musepackdec->srcpad, caps)) {
    GST_ELEMENT_ERROR (musepackdec, CORE, NEGOTIATION, (NULL), (NULL));
    return FALSE;
  }

  musepackdec->bps = 4 * i.channels;;
  musepackdec->rate = i.sample_freq;
  musepackdec->pos = 0;
  musepackdec->len = mpc_streaminfo_get_length_samples (&i);
  musepackdec->init = TRUE;

  return TRUE;
}

static void
gst_musepackdec_loop (GstElement * element)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (element);
  GstBuffer *out;
  GstFormat fmt;
  gint ret;
  guint32 update_acc, update_bits;

  if (!musepackdec->init) {
    if (!gst_musepack_stream_init (musepackdec))
      return;
  }

  if (musepackdec->seek_pending) {
    gdouble seek_time = (gdouble) musepackdec->seek_time / GST_SECOND;

    musepackdec->seek_pending = FALSE;
    if (mpc_decoder_seek_seconds (musepackdec->d, seek_time)) {
      if (musepackdec->flush_pending) {
        musepackdec->flush_pending = FALSE;
        gst_pad_push (musepackdec->srcpad,
            GST_DATA (gst_event_new (GST_EVENT_FLUSH)));
      }
      gst_pad_push (musepackdec->srcpad,
          GST_DATA (gst_event_new_discontinuous (FALSE,
                  GST_FORMAT_TIME, musepackdec->seek_time,
                  GST_FORMAT_UNDEFINED)));
      fmt = GST_FORMAT_DEFAULT;
      gst_pad_convert (musepackdec->srcpad,
          GST_FORMAT_TIME, musepackdec->seek_time,
          &fmt, (gint64 *) & musepackdec->pos);
    }
  }

  out = gst_buffer_new_and_alloc (MPC_DECODER_BUFFER_LENGTH * 4);
  ret = mpc_decoder_decode (musepackdec->d,
      (MPC_SAMPLE_FORMAT *) GST_BUFFER_DATA (out), &update_acc, &update_bits);
  if (ret <= 0) {
    if (ret == 0) {
      gst_element_set_eos (element);
      gst_pad_push (musepackdec->srcpad,
          GST_DATA (gst_event_new (GST_EVENT_EOS)));
    } else {
      GST_ERROR_OBJECT (musepackdec, "Failed to decode sample");
    }
    gst_buffer_unref (out);
    return;
  }

  GST_BUFFER_SIZE (out) = ret * musepackdec->bps;
  fmt = GST_FORMAT_TIME;
  gst_pad_query (musepackdec->srcpad,
      GST_QUERY_POSITION, &fmt, (gint64 *) & GST_BUFFER_TIMESTAMP (out));
  gst_pad_convert (musepackdec->srcpad,
      GST_FORMAT_BYTES, GST_BUFFER_SIZE (out),
      &fmt, (gint64 *) & GST_BUFFER_DURATION (out));
  musepackdec->pos += GST_BUFFER_SIZE (out) / musepackdec->bps;
  gst_pad_push (musepackdec->srcpad, GST_DATA (out));
}

static GstStateChangeReturn
gst_musepackdec_change_state (GstElement * element, GstStateChange transition)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      musepackdec->bs = gst_bytestream_new (musepackdec->sinkpad);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      musepackdec->seek_pending = FALSE;
      musepackdec->init = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_bytestream_destroy (musepackdec->bs);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_library_load ("gstbytestream") &&
      gst_element_register (plugin, "musepackdec",
      GST_RANK_PRIMARY, GST_TYPE_MUSEPACK_DEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "musepack",
    "Musepack decoder", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
