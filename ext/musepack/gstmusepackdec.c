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
static const GstQueryType *gst_musepackdec_get_src_query_types (GstPad * pad);
static gboolean gst_musepackdec_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_musepackdec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_musepackdec_sink_activate (GstPad * sinkpad);
static gboolean
gst_musepackdec_sink_activate_pull (GstPad * sinkpad, gboolean active);

static void gst_musepackdec_loop (GstPad * sinkpad);
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
  musepackdec->offset = 0;

  musepackdec->r = g_new (mpc_reader, 1);
  musepackdec->d = g_new (mpc_decoder, 1);
  musepackdec->init = FALSE;
  musepackdec->seek_pending = FALSE;
  musepackdec->flush_pending = FALSE;
  musepackdec->eos = FALSE;

  musepackdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_event_function (musepackdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_sink_event));
  gst_element_add_pad (GST_ELEMENT (musepackdec), musepackdec->sinkpad);

  gst_pad_set_activate_function (musepackdec->sinkpad,
      gst_musepackdec_sink_activate);
  gst_pad_set_activatepull_function (musepackdec->sinkpad,
      gst_musepackdec_sink_activate_pull);

  musepackdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_set_event_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_src_event));

  gst_pad_set_query_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_src_query));
  gst_pad_set_query_type_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_get_src_query_types));
  gst_pad_use_fixed_caps (musepackdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (musepackdec), musepackdec->srcpad);


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
gst_musepackdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      musepackdec->flush_pending = TRUE;
      goto done;
      break;
    case GST_EVENT_NEWSEGMENT:
      musepackdec->flush_pending = TRUE;
      musepackdec->seek_pending = TRUE;
      goto done;
      break;
    case GST_EVENT_EOS:
      musepackdec->eos = TRUE;
      /* fall through */
    default:
      res = gst_pad_event_default (pad, event);
      gst_object_unref (musepackdec);
      return res;
      break;
  }

done:
  gst_event_unref (event);
  gst_object_unref (musepackdec);
  return res;

}


static gboolean
gst_musepackdec_src_event (GstPad * pad, GstEvent * event)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{

      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType cur_type;
      gint64 cur;
      GstSeekType stop_type;
      gint64 stop;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);


      gint64 offset, len, pos;
      GstFormat fmt = GST_FORMAT_TIME;

      if (!gst_musepackdec_src_convert (pad, format, cur, &fmt, &offset)) {

      }
      if (!gst_musepackdec_src_convert (pad, GST_FORMAT_DEFAULT,
              musepackdec->len, &fmt, &len)) {
        res = FALSE;
        break;
      }
      if (!gst_musepackdec_src_convert (pad, GST_FORMAT_DEFAULT,
              musepackdec->pos, &fmt, &pos)) {
        res = FALSE;
        break;
      }

      /* offset from start */
      switch (cur_type) {
        case GST_SEEK_TYPE_SET:
          break;
        case GST_SEEK_TYPE_CUR:
          offset += pos;
          break;
        case GST_SEEK_TYPE_END:
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
      musepackdec->flush_pending = flags & GST_SEEK_FLAG_FLUSH;
      musepackdec->seek_time = offset;
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      gst_object_unref (musepackdec);
      return res;
      break;
  }

done:
  gst_event_unref (event);
  gst_object_unref (musepackdec);
  return res;
}

static const GstQueryType *
gst_musepackdec_get_src_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    (GstQueryType) 0
  };

  return query_types;
}

static gboolean
gst_musepackdec_src_query (GstPad * pad, GstQuery * query)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  GstFormat format = GST_FORMAT_DEFAULT;
  GstFormat dest_format;
  gint64 value, dest_value;
  gboolean res = TRUE;

  if (!musepackdec->init) {
    res = FALSE;
    goto done;
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &dest_format, NULL);
      if (!gst_musepackdec_src_convert (pad, format, musepackdec->pos,
              &dest_format, &dest_value)) {
        res = FALSE;
      }
      gst_query_set_position (query, dest_format, dest_value);
      break;
    case GST_QUERY_DURATION:
      gst_query_parse_duration (query, &dest_format, NULL);
      if (!gst_musepackdec_src_convert (pad, format, musepackdec->len,
              &dest_format, &dest_value)) {
        res = FALSE;
        break;
      }
      gst_query_set_duration (query, dest_format, dest_value);
      break;
    case GST_QUERY_CONVERT:
      gst_query_parse_convert (query, &format, &value, &dest_format,
          &dest_value);
      if (!gst_musepackdec_src_convert (pad, format, value, &dest_format,
              &dest_value)) {
        res = FALSE;
      }
      gst_query_set_convert (query, format, value, dest_format, dest_value);
      break;
    default:
      res = FALSE;
      break;
  }

done:
  g_object_unref (musepackdec);
  return res;
}

gboolean
gst_musepackdec_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  if (!musepackdec->init) {
    gst_object_unref (musepackdec);
    return FALSE;
  }

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

  gst_object_unref (musepackdec);
  return TRUE;
}

static gboolean
gst_musepack_stream_init (GstMusepackDec * musepackdec)
{
  mpc_streaminfo i;
  GstCaps *caps;

  /* set up reading */
  gst_musepack_init_reader (musepackdec->r, musepackdec);

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
  gst_pad_use_fixed_caps (musepackdec->srcpad);
  if (!gst_pad_set_caps (musepackdec->srcpad, caps)) {
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

static gboolean
gst_musepackdec_sink_activate (GstPad * sinkpad)
{

  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return FALSE;
  }
}

static gboolean
gst_musepackdec_sink_activate_pull (GstPad * sinkpad, gboolean active)
{

  gboolean result;

  if (active) {

    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_musepackdec_loop, sinkpad);
  } else {
    result = gst_pad_stop_task (sinkpad);
  }

  return result;
}

static void
gst_musepackdec_loop (GstPad * sinkpad)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (GST_PAD_PARENT (sinkpad));
  GstBuffer *out;
  GstFormat fmt;
  gint ret;
  guint32 update_acc, update_bits;

  if (!musepackdec->init) {
    if (!gst_musepack_stream_init (musepackdec))
      return;
    gst_pad_push_event (musepackdec->srcpad,
        gst_event_new_new_segment (FALSE, 1.0,
            GST_FORMAT_TIME, musepackdec->pos, GST_CLOCK_TIME_NONE, 0));
  }

  if (musepackdec->seek_pending) {
    gdouble seek_time = (gdouble) musepackdec->seek_time / GST_SECOND;

    musepackdec->seek_pending = FALSE;
    if (mpc_decoder_seek_seconds (musepackdec->d, seek_time)) {
      if (musepackdec->flush_pending) {
        musepackdec->flush_pending = FALSE;
        gst_pad_push_event (musepackdec->srcpad, gst_event_new_flush_start ());
      }
      gst_pad_push_event (musepackdec->srcpad,
          gst_event_new_new_segment (FALSE, 1.0,
              GST_FORMAT_TIME, musepackdec->seek_time, GST_CLOCK_TIME_NONE, 0));
      fmt = GST_FORMAT_DEFAULT;
      gst_musepackdec_src_convert (musepackdec->srcpad,
          GST_FORMAT_TIME, musepackdec->seek_time,
          &fmt, (gint64 *) & musepackdec->pos);
    }
  }

  out = gst_buffer_new_and_alloc (MPC_DECODER_BUFFER_LENGTH * 4);
  ret = mpc_decoder_decode (musepackdec->d,
      (MPC_SAMPLE_FORMAT *) GST_BUFFER_DATA (out), &update_acc, &update_bits);
  if (ret <= 0 || musepackdec->eos) {
    if (ret < 0) {
      GST_ERROR_OBJECT (musepackdec, "Failed to decode sample");
    } else if (!musepackdec->eos) {
      musepackdec->eos = TRUE;
      gst_pad_push_event (musepackdec->sinkpad, gst_event_new_eos ());
    }
    gst_buffer_unref (out);
    return;
  }

  GST_BUFFER_SIZE (out) = ret * musepackdec->bps;
  fmt = GST_FORMAT_TIME;

  gint64 value;

  gst_musepackdec_src_convert (musepackdec->srcpad,
      GST_FORMAT_BYTES, GST_BUFFER_SIZE (out), &fmt, &value);
  GST_BUFFER_DURATION (out) = value;

  gst_musepackdec_src_convert (musepackdec->srcpad,
      GST_FORMAT_DEFAULT, musepackdec->pos, &fmt, &value);
  GST_BUFFER_TIMESTAMP (out) = value;

  musepackdec->pos += GST_BUFFER_SIZE (out) / musepackdec->bps;
  gst_buffer_set_caps (out, GST_PAD_CAPS (musepackdec->srcpad));
  gst_pad_push (musepackdec->srcpad, out);
}

static GstStateChangeReturn
gst_musepackdec_change_state (GstElement * element, GstStateChange transition)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);


  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      musepackdec->seek_pending = FALSE;
      musepackdec->init = FALSE;
      break;
    default:
      break;
  }

  return ret;

}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "musepackdec",
      GST_RANK_PRIMARY, GST_TYPE_MUSEPACK_DEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "musepack",
    "Musepack decoder", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
