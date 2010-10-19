/* GStreamer Adaptive Multi-Rate Narrow-Band (AMR-NB) plugin
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

/**
 * SECTION:element-amrnbenc
 * @see_also: #GstAmrnbDec, #GstAmrnbParse
 *
 * AMR narrowband encoder based on the 
 * <ulink url="http://sourceforge.net/projects/opencore-amr">opencore codec implementation</ulink>.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.wav ! wavparse ! audioresample ! audioconvert ! amrnbenc ! filesink location=abc.amr
 * ]|
 * Please note that the above stream misses the header, that is needed to play
 * the stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "amrnbenc.h"

static GType
gst_amrnbenc_bandmode_get_type (void)
{
  static GType gst_amrnbenc_bandmode_type = 0;
  static const GEnumValue gst_amrnbenc_bandmode[] = {
    {MR475, "MR475", "MR475"},
    {MR515, "MR515", "MR515"},
    {MR59, "MR59", "MR59"},
    {MR67, "MR67", "MR67"},
    {MR74, "MR74", "MR74"},
    {MR795, "MR795", "MR795"},
    {MR102, "MR102", "MR102"},
    {MR122, "MR122", "MR122"},
    {MRDTX, "MRDTX", "MRDTX"},
    {0, NULL, NULL},
  };
  if (!gst_amrnbenc_bandmode_type) {
    gst_amrnbenc_bandmode_type =
        g_enum_register_static ("GstAmrnbEncBandMode", gst_amrnbenc_bandmode);
  }
  return gst_amrnbenc_bandmode_type;
}

#define GST_AMRNBENC_BANDMODE_TYPE (gst_amrnbenc_bandmode_get_type())

#define BANDMODE_DEFAULT MR122
enum
{
  PROP_0,
  PROP_BANDMODE
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) 8000," "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "rate = (int) 8000, " "channels = (int) 1")
    );

GST_DEBUG_CATEGORY_STATIC (gst_amrnbenc_debug);
#define GST_CAT_DEFAULT gst_amrnbenc_debug

static void gst_amrnbenc_finalize (GObject * object);

static GstFlowReturn gst_amrnbenc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_amrnbenc_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_amrnbenc_state_change (GstElement * element,
    GstStateChange transition);

static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface init */
    NULL,                       /* interface finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);

  GST_DEBUG_CATEGORY_INIT (gst_amrnbenc_debug, "amrnbenc", 0,
      "AMR-NB audio encoder");
}

GST_BOILERPLATE_FULL (GstAmrnbEnc, gst_amrnbenc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_amrnbenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmrnbEnc *self = GST_AMRNBENC (object);

  switch (prop_id) {
    case PROP_BANDMODE:
      self->bandmode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_amrnbenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmrnbEnc *self = GST_AMRNBENC (object);

  switch (prop_id) {
    case PROP_BANDMODE:
      g_value_set_enum (value, self->bandmode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_amrnbenc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class, "AMR-NB audio encoder",
      "Codec/Encoder/Audio",
      "Adaptive Multi-Rate Narrow-Band audio encoder",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_amrnbenc_class_init (GstAmrnbEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = gst_amrnbenc_set_property;
  object_class->get_property = gst_amrnbenc_get_property;
  object_class->finalize = gst_amrnbenc_finalize;

  g_object_class_install_property (object_class, PROP_BANDMODE,
      g_param_spec_enum ("band-mode", "Band Mode",
          "Encoding Band Mode (Kbps)", GST_AMRNBENC_BANDMODE_TYPE,
          BANDMODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_amrnbenc_state_change);
}

static void
gst_amrnbenc_init (GstAmrnbEnc * amrnbenc, GstAmrnbEncClass * klass)
{
  /* create the sink pad */
  amrnbenc->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (amrnbenc->sinkpad, gst_amrnbenc_setcaps);
  gst_pad_set_chain_function (amrnbenc->sinkpad, gst_amrnbenc_chain);
  gst_element_add_pad (GST_ELEMENT (amrnbenc), amrnbenc->sinkpad);

  /* create the src pad */
  amrnbenc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (amrnbenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrnbenc), amrnbenc->srcpad);

  amrnbenc->adapter = gst_adapter_new ();

  /* init rest */
  amrnbenc->handle = NULL;
}

static void
gst_amrnbenc_finalize (GObject * object)
{
  GstAmrnbEnc *amrnbenc;

  amrnbenc = GST_AMRNBENC (object);

  g_object_unref (G_OBJECT (amrnbenc->adapter));
  amrnbenc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_amrnbenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrnbEnc *amrnbenc;
  GstCaps *copy;

  amrnbenc = GST_AMRNBENC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrnbenc->channels);
  gst_structure_get_int (structure, "rate", &amrnbenc->rate);

  /* this is not wrong but will sound bad */
  if (amrnbenc->channels != 1) {
    g_warning ("amrnbdec is only optimized for mono channels");
  }
  if (amrnbenc->rate != 8000) {
    g_warning ("amrnbdec is only optimized for 8000 Hz samplerate");
  }

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/AMR",
      "channels", G_TYPE_INT, amrnbenc->channels,
      "rate", G_TYPE_INT, amrnbenc->rate, NULL);

  /* precalc duration as it's constant now */
  amrnbenc->duration = gst_util_uint64_scale_int (160, GST_SECOND,
      amrnbenc->rate * amrnbenc->channels);

  gst_pad_set_caps (amrnbenc->srcpad, copy);
  gst_caps_unref (copy);

  return TRUE;
}

static GstFlowReturn
gst_amrnbenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrnbEnc *amrnbenc;
  GstFlowReturn ret;

  amrnbenc = GST_AMRNBENC (GST_PAD_PARENT (pad));

  g_return_val_if_fail (amrnbenc->handle, GST_FLOW_WRONG_STATE);

  if (amrnbenc->rate == 0 || amrnbenc->channels == 0)
    goto not_negotiated;

  /* discontinuity clears adapter, FIXME, maybe we can set some
   * encoder flag to mask the discont. */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (amrnbenc->adapter);
    amrnbenc->ts = 0;
    amrnbenc->discont = TRUE;
  }

  /* take latest timestamp, FIXME timestamp is the one of the
   * first buffer in the adapter. */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrnbenc->ts = GST_BUFFER_TIMESTAMP (buffer);

  ret = GST_FLOW_OK;
  gst_adapter_push (amrnbenc->adapter, buffer);

  /* Collect samples until we have enough for an output frame */
  while (gst_adapter_available (amrnbenc->adapter) >= 320) {
    GstBuffer *out;
    guint8 *data;
    gint outsize;

    /* get output, max size is 32 */
    out = gst_buffer_new_and_alloc (32);
    GST_BUFFER_DURATION (out) = amrnbenc->duration;
    GST_BUFFER_TIMESTAMP (out) = amrnbenc->ts;
    if (amrnbenc->ts != -1) {
      amrnbenc->ts += amrnbenc->duration;
    }
    if (amrnbenc->discont) {
      GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
      amrnbenc->discont = FALSE;
    }

    gst_buffer_set_caps (out, GST_PAD_CAPS (amrnbenc->srcpad));

    /* The AMR encoder actually writes into the source data buffers it gets */
    data = gst_adapter_take (amrnbenc->adapter, 320);

    /* encode */
    outsize =
        Encoder_Interface_Encode (amrnbenc->handle, amrnbenc->bandmode,
        (short *) data, (guint8 *) GST_BUFFER_DATA (out), 0);

    g_free (data);

    GST_BUFFER_SIZE (out) = outsize;

    /* play */
    if ((ret = gst_pad_push (amrnbenc->srcpad, out)) != GST_FLOW_OK)
      break;
  }
  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (amrnbenc, STREAM, TYPE_NOT_FOUND,
        (NULL), ("unknown type"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstStateChangeReturn
gst_amrnbenc_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrnbEnc *amrnbenc;
  GstStateChangeReturn ret;

  amrnbenc = GST_AMRNBENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(amrnbenc->handle = Encoder_Interface_init (0)))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      amrnbenc->rate = 0;
      amrnbenc->channels = 0;
      amrnbenc->ts = 0;
      amrnbenc->discont = FALSE;
      gst_adapter_clear (amrnbenc->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      Encoder_Interface_exit (amrnbenc->handle);
      break;
    default:
      break;
  }
  return ret;
}
