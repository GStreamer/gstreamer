/* GStreamer FAAD (Free AAC Decoder) plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>

#include "gstfaad.h"

GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "systemstream = (bool) FALSE, " "mpegversion = { (int) 2, (int) 4 }")
    );

GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (bool) TRUE, "
        "width = (int) { 16, 24, 32 }, "
        "depth = (int) { 16, 24, 32 }, "
        "rate = (int) [ 8000, 96000 ], "
        "channels = (int) [ 1, 6 ]; "
        "audio/x-raw-float, "
        "endianness = (int) BYTE_ORDER, "
        "depth = (int) { 32, 64 }, "
        "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 6 ]")
    );

static void gst_faad_base_init (GstFaadClass * klass);
static void gst_faad_class_init (GstFaadClass * klass);
static void gst_faad_init (GstFaad * faad);

static GstPadLinkReturn
gst_faad_sinkconnect (GstPad * pad, const GstCaps * caps);
static GstPadLinkReturn
gst_faad_srcconnect (GstPad * pad, const GstCaps * caps);
static GstCaps *gst_faad_srcgetcaps (GstPad * pad);
static void gst_faad_chain (GstPad * pad, GstData * data);
static GstElementStateReturn gst_faad_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/* static guint gst_faad_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_faad_get_type (void)
{
  static GType gst_faad_type = 0;

  if (!gst_faad_type) {
    static const GTypeInfo gst_faad_info = {
      sizeof (GstFaadClass),
      (GBaseInitFunc) gst_faad_base_init,
      NULL,
      (GClassInitFunc) gst_faad_class_init,
      NULL,
      NULL,
      sizeof (GstFaad),
      0,
      (GInstanceInitFunc) gst_faad_init,
    };

    gst_faad_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFaad", &gst_faad_info, 0);
  }

  return gst_faad_type;
}

static void
gst_faad_base_init (GstFaadClass * klass)
{
  GstElementDetails gst_faad_details = {
    "Free AAC Decoder (FAAD)",
    "Codec/Audio/Decoder",
    "Free MPEG-2/4 AAC decoder",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_faad_details);
}

static void
gst_faad_class_init (GstFaadClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_faad_change_state;
}

static void
gst_faad_init (GstFaad * faad)
{
  faad->handle = NULL;
  faad->samplerate = -1;
  faad->channels = -1;

  GST_FLAG_SET (faad, GST_ELEMENT_EVENT_AWARE);

  faad->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (faad), faad->sinkpad);
  gst_pad_set_chain_function (faad->sinkpad, gst_faad_chain);
  gst_pad_set_link_function (faad->sinkpad, gst_faad_sinkconnect);

  faad->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (faad), faad->srcpad);
  gst_pad_set_link_function (faad->srcpad, gst_faad_srcconnect);

  gst_pad_set_getcaps_function (faad->srcpad, gst_faad_srcgetcaps);
}

static GstPadLinkReturn
gst_faad_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *value;
  GstBuffer *buf;

  if ((value = gst_structure_get_value (str, "codec_data"))) {
    GstPadLinkReturn ret;
    gulong samplerate;
    guchar channels;

    buf = g_value_get_boxed (value);
    /* someone forgot that char can be unsigned when writing the API */
    if ((gint8) faacDecInit2 (faad->handle, GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf), &samplerate, &channels) < 0)
      return GST_PAD_LINK_REFUSED;

    faad->samplerate = samplerate;
    faad->channels = channels;

    ret = gst_pad_renegotiate (faad->srcpad);
    if (ret == GST_PAD_LINK_DELAYED)
      ret = GST_PAD_LINK_OK;

    return ret;
  }

  /* if there's no decoderspecificdata, it's all fine. We cannot know
   * much more at this point... */
  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_faad_srcgetcaps (GstPad * pad)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));

  if (faad->handle != NULL && faad->channels != -1 && faad->samplerate != -1) {
    GstCaps *caps = gst_caps_new_empty ();
    GstStructure *str;
    gint fmt[] = {
      FAAD_FMT_16BIT,
      FAAD_FMT_24BIT,
      FAAD_FMT_32BIT,
      FAAD_FMT_FLOAT,
      FAAD_FMT_DOUBLE,
      -1
    }
    , n;

    for (n = 0; fmt[n] != -1; n++) {
      switch (n) {
        case FAAD_FMT_16BIT:
          str = gst_structure_new ("audio/x-raw-int",
              "signed", G_TYPE_BOOLEAN, TRUE,
              "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
          break;
        case FAAD_FMT_24BIT:
          str = gst_structure_new ("audio/x-raw-int",
              "signed", G_TYPE_BOOLEAN, TRUE,
              "width", G_TYPE_INT, 24, "depth", G_TYPE_INT, 24, NULL);
          break;
        case FAAD_FMT_32BIT:
          str = gst_structure_new ("audio/x-raw-int",
              "signed", G_TYPE_BOOLEAN, TRUE,
              "width", G_TYPE_INT, 32, "depth", G_TYPE_INT, 32, NULL);
          break;
        case FAAD_FMT_FLOAT:
          str = gst_structure_new ("audio/x-raw-float",
              "depth", G_TYPE_INT, 32, NULL);
          break;
        case FAAD_FMT_DOUBLE:
          str = gst_structure_new ("audio/x-raw-float",
              "depth", G_TYPE_INT, 64, NULL);
          break;
        default:
          str = NULL;
          break;
      }
      if (!str)
        continue;

      if (faad->samplerate != -1) {
        gst_structure_set (str, "rate", G_TYPE_INT, faad->samplerate, NULL);
      } else {
        gst_structure_set (str, "rate", GST_TYPE_INT_RANGE, 8000, 96000, NULL);
      }

      if (faad->channels != -1) {
        gst_structure_set (str, "channels", G_TYPE_INT, faad->channels, NULL);
      } else {
        gst_structure_set (str, "channels", GST_TYPE_INT_RANGE, 1, 6, NULL);
      }

      gst_structure_set (str, "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);

      gst_caps_append_structure (caps, str);
    }

    return caps;
  }

  return gst_caps_copy (GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad)));
}

static GstPadLinkReturn
gst_faad_srcconnect (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  const gchar *mimetype;
  gint fmt = -1;
  gint depth, rate, channels;
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));

  if (!faad->handle || (faad->samplerate == -1 || faad->channels == -1)) {
    return GST_PAD_LINK_DELAYED;
  }

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* Samplerate and channels are normally provided through
   * the getcaps function */
  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate) ||
      rate != faad->samplerate || channels != faad->channels) {
    return GST_PAD_LINK_REFUSED;
  }

  if (!strcmp (mimetype, "audio/x-raw-int")) {
    gint width;

    if (!gst_structure_get_int (structure, "depth", &depth) ||
        !gst_structure_get_int (structure, "width", &width))
      return GST_PAD_LINK_REFUSED;
    if (depth != width)
      return GST_PAD_LINK_REFUSED;

    switch (depth) {
      case 16:
        fmt = FAAD_FMT_16BIT;
        break;
      case 24:
        fmt = FAAD_FMT_24BIT;
        break;
      case 32:
        fmt = FAAD_FMT_32BIT;
        break;
    }
  } else {
    if (!gst_structure_get_int (structure, "depth", &depth))
      return GST_PAD_LINK_REFUSED;

    switch (depth) {
      case 32:
        fmt = FAAD_FMT_FLOAT;
        break;
      case 64:
        fmt = FAAD_FMT_DOUBLE;
        break;
    }
  }

  if (fmt != -1) {
    faacDecConfiguration *conf;

    conf = faacDecGetCurrentConfiguration (faad->handle);
    conf->outputFormat = fmt;
    faacDecSetConfiguration (faad->handle, conf);
    /* FIXME: handle return value, how? */
    faad->bps = depth / 8;

    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
}

static void
gst_faad_chain (GstPad * pad, GstData * data)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  GstBuffer *buf, *outbuf;
  faacDecFrameInfo info;
  void *out;

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        gst_element_set_eos (GST_ELEMENT (faad));
        gst_pad_push (faad->srcpad, data);
        return;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
  }

  buf = GST_BUFFER (data);

  if (faad->samplerate == -1 || faad->channels == -1) {
    GstPadLinkReturn ret;
    gulong samplerate;
    guchar channels;

    faacDecInit (faad->handle,
        GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), &samplerate, &channels);
    faad->samplerate = samplerate;
    faad->channels = channels;
    ret = gst_pad_renegotiate (faad->srcpad);
    if (GST_PAD_LINK_FAILED (ret)) {
      GST_ELEMENT_ERROR (faad, CORE, NEGOTIATION, (NULL), (NULL));
      gst_buffer_unref (buf);
      return;
    }
  }

  out = faacDecDecode (faad->handle, &info,
      GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  if (info.error) {
    GST_ELEMENT_ERROR (faad, STREAM, DECODE, (NULL),
        ("Failed to decode buffer: %s", faacDecGetErrorMessage (info.error)));
    gst_buffer_unref (buf);
    return;
  }

  if (info.samplerate != faad->samplerate || info.channels != faad->channels) {
    GstPadLinkReturn ret;

    faad->samplerate = info.samplerate;
    faad->channels = info.channels;
    ret = gst_pad_renegotiate (faad->srcpad);
    if (GST_PAD_LINK_FAILED (ret)) {
      GST_ELEMENT_ERROR (faad, CORE, NEGOTIATION, (NULL), (NULL));
      gst_buffer_unref (buf);
      return;
    }
  }

  if (info.samples == 0) {
    gst_buffer_unref (buf);
    return;
  }

  /* FIXME: did it handle the whole buffer? */
  outbuf = gst_buffer_new_and_alloc (info.samples * faad->bps);
  /* ugh */
  memcpy (GST_BUFFER_DATA (outbuf), out, GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  gst_buffer_unref (buf);
  gst_pad_push (faad->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_faad_change_state (GstElement * element)
{
  GstFaad *faad = GST_FAAD (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!(faad->handle = faacDecOpen ()))
        return GST_STATE_FAILURE;
      else {
        faacDecConfiguration *conf;

        conf = faacDecGetCurrentConfiguration (faad->handle);
        conf->defObjectType = LC;
        faacDecSetConfiguration (faad->handle, conf);
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      faad->samplerate = -1;
      faad->channels = -1;
      break;
    case GST_STATE_READY_TO_NULL:
      faacDecClose (faad->handle);
      faad->handle = NULL;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "faad", GST_RANK_PRIMARY, GST_TYPE_FAAD);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "faad",
    "Free AAC Decoder (FAAD)",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
