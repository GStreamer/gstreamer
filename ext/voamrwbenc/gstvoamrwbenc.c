/* GStreamer Adaptive Multi-Rate Wide-Band (AMR-WB) plugin
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
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
 * SECTION:element-voamrwbenc
 * @see_also: #GstAmrWbDec, #GstAmrWbParse
 *
 * AMR wideband encoder based on the 
 * <ulink url="http://www.penguin.cz/~utx/amr">reference codec implementation</ulink>.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.wav ! wavparse ! audioresample ! audioconvert ! voamrwbenc ! filesink location=abc.amr
 * ]|
 * Please note that the above stream misses the header, that is needed to play
 * the stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvoamrwbenc.h"

#define MR660  0
#define MR885  1
#define MR1265 2
#define MR1425 2
#define MR1585 3
#define MR1825 4
#define MR1985 5
#define MR2305 6
#define MR2385 7
#define MRDTX  8

#define L_FRAME16k      320     /* Frame size at 16kHz  */

static GType
gst_voamrwbenc_bandmode_get_type (void)
{
  static GType gst_voamrwbenc_bandmode_type = 0;
  static GEnumValue gst_voamrwbenc_bandmode[] = {
    {MR660, "MR660", "MR660"},
    {MR885, "MR885", "MR885"},
    {MR1265, "MR1265", "MR1265"},
    {MR1425, "MR1425", "MR1425"},
    {MR1585, "MR1585", "MR1585"},
    {MR1825, "MR1825", "MR1825"},
    {MR1985, "MR1985", "MR1985"},
    {MR2305, "MR2305", "MR2305"},
    {MR2385, "MR2385", "MR2385"},
    {MRDTX, "MRDTX", "MRDTX"},
    {0, NULL, NULL},
  };
  if (!gst_voamrwbenc_bandmode_type) {
    gst_voamrwbenc_bandmode_type =
        g_enum_register_static ("GstVoAmrWbEncBandMode",
        gst_voamrwbenc_bandmode);
  }
  return gst_voamrwbenc_bandmode_type;
}

#define GST_VOAMRWBENC_BANDMODE_TYPE (gst_voamrwbenc_bandmode_get_type())

#define BANDMODE_DEFAULT MR660

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
        "rate = (int) 16000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR-WB, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

GST_DEBUG_CATEGORY_STATIC (gst_voamrwbenc_debug);
#define GST_CAT_DEFAULT gst_voamrwbenc_debug

static void gst_voamrwbenc_finalize (GObject * object);

static GstFlowReturn gst_voamrwbenc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_voamrwbenc_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_voamrwbenc_state_change (GstElement * element,
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

  GST_DEBUG_CATEGORY_INIT (gst_voamrwbenc_debug, "amrwbenc", 0,
      "AMR-WB audio encoder");
}

GST_BOILERPLATE_FULL (GstVoAmrWbEnc, gst_voamrwbenc, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_voamrwbenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVoAmrWbEnc *self = GST_VOAMRWBENC (object);

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
gst_voamrwbenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVoAmrWbEnc *self = GST_VOAMRWBENC (object);

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
gst_voamrwbenc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class, "AMR-WB audio encoder",
      "Codec/Encoder/Audio",
      "Adaptive Multi-Rate Wideband audio encoder",
      "Renato Araujo <renato.filho@indt.org.br>");
}

static void
gst_voamrwbenc_class_init (GstVoAmrWbEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->finalize = gst_voamrwbenc_finalize;
  object_class->set_property = gst_voamrwbenc_set_property;
  object_class->get_property = gst_voamrwbenc_get_property;

  g_object_class_install_property (object_class, PROP_BANDMODE,
      g_param_spec_enum ("band-mode", "Band Mode",
          "Encoding Band Mode (Kbps)", GST_VOAMRWBENC_BANDMODE_TYPE,
          BANDMODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_voamrwbenc_state_change);
}

static void
gst_voamrwbenc_init (GstVoAmrWbEnc * amrwbenc, GstVoAmrWbEncClass * klass)
{
  /* create the sink pad */
  amrwbenc->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (amrwbenc->sinkpad, gst_voamrwbenc_setcaps);
  gst_pad_set_chain_function (amrwbenc->sinkpad, gst_voamrwbenc_chain);
  gst_element_add_pad (GST_ELEMENT (amrwbenc), amrwbenc->sinkpad);

  /* create the src pad */
  amrwbenc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (amrwbenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrwbenc), amrwbenc->srcpad);

  amrwbenc->adapter = gst_adapter_new ();

  /* init rest */
  amrwbenc->handle = NULL;
  amrwbenc->channels = 0;
  amrwbenc->rate = 0;
  amrwbenc->ts = 0;
}

static void
gst_voamrwbenc_finalize (GObject * object)
{
  GstVoAmrWbEnc *amrwbenc;

  amrwbenc = GST_VOAMRWBENC (object);

  g_object_unref (G_OBJECT (amrwbenc->adapter));
  amrwbenc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_voamrwbenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstVoAmrWbEnc *amrwbenc;
  GstCaps *copy;

  amrwbenc = GST_VOAMRWBENC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrwbenc->channels);
  gst_structure_get_int (structure, "rate", &amrwbenc->rate);

  /* this is not wrong but will sound bad */
  if (amrwbenc->channels != 1) {
    GST_WARNING ("amrwbdec is only optimized for mono channels");
  }
  if (amrwbenc->rate != 16000) {
    GST_WARNING ("amrwbdec is only optimized for 16000 Hz samplerate");
  }

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/AMR-WB",
      "channels", G_TYPE_INT, amrwbenc->channels,
      "rate", G_TYPE_INT, amrwbenc->rate, NULL);

  gst_pad_set_caps (amrwbenc->srcpad, copy);
  gst_caps_unref (copy);

  return TRUE;
}

static GstFlowReturn
gst_voamrwbenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVoAmrWbEnc *amrwbenc;
  GstFlowReturn ret = GST_FLOW_OK;
  const int buffer_size = sizeof (short) * L_FRAME16k;

  amrwbenc = GST_VOAMRWBENC (gst_pad_get_parent (pad));

  g_return_val_if_fail (amrwbenc->handle, GST_FLOW_WRONG_STATE);

  if (amrwbenc->rate == 0 || amrwbenc->channels == 0) {
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

  /* discontinuity clears adapter, FIXME, maybe we can set some
   * encoder flag to mask the discont. */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (amrwbenc->adapter);
    amrwbenc->ts = 0;
    amrwbenc->discont = TRUE;
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrwbenc->ts = GST_BUFFER_TIMESTAMP (buffer);

  ret = GST_FLOW_OK;
  gst_adapter_push (amrwbenc->adapter, buffer);

  /* Collect samples until we have enough for an output frame */
  while (gst_adapter_available (amrwbenc->adapter) >= buffer_size) {
    GstBuffer *out;
    guint8 *data;
    gint outsize;

    out = gst_buffer_new_and_alloc (buffer_size);
    GST_BUFFER_DURATION (out) = GST_SECOND * L_FRAME16k /
        (amrwbenc->rate * amrwbenc->channels);
    GST_BUFFER_TIMESTAMP (out) = amrwbenc->ts;
    if (amrwbenc->ts != -1) {
      amrwbenc->ts += GST_BUFFER_DURATION (out);
    }
    if (amrwbenc->discont) {
      GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
      amrwbenc->discont = FALSE;
    }
    gst_buffer_set_caps (out, gst_pad_get_caps (amrwbenc->srcpad));

    data = (guint8 *) gst_adapter_peek (amrwbenc->adapter, buffer_size);

    /* encode */
    outsize =
        E_IF_encode (amrwbenc->handle, amrwbenc->bandmode, (const short *) data,
        (unsigned char *) GST_BUFFER_DATA (out), 0);

    gst_adapter_flush (amrwbenc->adapter, buffer_size);
    GST_BUFFER_SIZE (out) = outsize;

    /* play */
    if ((ret = gst_pad_push (amrwbenc->srcpad, out)) != GST_FLOW_OK)
      break;
  }

done:

  gst_object_unref (amrwbenc);
  return ret;

}

static GstStateChangeReturn
gst_voamrwbenc_state_change (GstElement * element, GstStateChange transition)
{
  GstVoAmrWbEnc *amrwbenc;
  GstStateChangeReturn ret;

  amrwbenc = GST_VOAMRWBENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(amrwbenc->handle = E_IF_init ()))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      amrwbenc->rate = 0;
      amrwbenc->channels = 0;
      amrwbenc->ts = 0;
      amrwbenc->discont = FALSE;
      gst_adapter_clear (amrwbenc->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      E_IF_exit (amrwbenc->handle);
      break;
    default:
      break;
  }

  return ret;
}
