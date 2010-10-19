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
 * SECTION:element-amrnbdec
 * @see_also: #GstAmrnbEnc, #GstAmrParse
 *
 * AMR narrowband decoder based on the 
 * <ulink url="http://sourceforge.net/projects/opencore-amr">opencore codec implementation</ulink>.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.amr ! amrparse ! amrnbdec ! audioresample ! audioconvert ! alsasink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "amrnbdec.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) 8000," "channels = (int) 1")
    );

GST_DEBUG_CATEGORY_STATIC (gst_amrnbdec_debug);
#define GST_CAT_DEFAULT gst_amrnbdec_debug

static const gint block_size_if1[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5,
  0, 0, 0, 0, 0, 0, 0
};

static const gint block_size_if2[16] = { 12, 13, 15, 17, 18, 20, 25, 30, 5,
  0, 0, 0, 0, 0, 0, 0
};

static GType
gst_amrnb_variant_get_type (void)
{
  static GType gst_amrnb_variant_type = 0;
  static const GEnumValue gst_amrnb_variant[] = {
    {GST_AMRNB_VARIANT_IF1, "IF1", "IF1"},
    {GST_AMRNB_VARIANT_IF2, "IF2", "IF2"},
    {0, NULL, NULL},
  };
  if (!gst_amrnb_variant_type) {
    gst_amrnb_variant_type =
        g_enum_register_static ("GstAmrnbVariant", gst_amrnb_variant);
  }
  return gst_amrnb_variant_type;
}

#define GST_AMRNB_VARIANT_TYPE (gst_amrnb_variant_get_type())

#define VARIANT_DEFAULT GST_AMRNB_VARIANT_IF1
enum
{
  PROP_0,
  PROP_VARIANT
};

static void gst_amrnbdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amrnbdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_amrnbdec_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_amrnbdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_amrnbdec_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_amrnbdec_state_change (GstElement * element,
    GstStateChange transition);

static void gst_amrnbdec_finalize (GObject * object);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_amrnbdec_debug, "amrnbdec", 0, "AMR-NB audio decoder");

GST_BOILERPLATE_FULL (GstAmrnbDec, gst_amrnbdec, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_amrnbdec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class, "AMR-NB audio decoder",
      "Codec/Decoder/Audio",
      "Adaptive Multi-Rate Narrow-Band audio decoder",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");
}

static void
gst_amrnbdec_class_init (GstAmrnbDecClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = gst_amrnbdec_set_property;
  object_class->get_property = gst_amrnbdec_get_property;
  object_class->finalize = gst_amrnbdec_finalize;

  g_object_class_install_property (object_class, PROP_VARIANT,
      g_param_spec_enum ("variant", "Variant",
          "The decoder variant", GST_AMRNB_VARIANT_TYPE,
          VARIANT_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_amrnbdec_state_change);
}

static void
gst_amrnbdec_init (GstAmrnbDec * amrnbdec, GstAmrnbDecClass * klass)
{
  /* create the sink pad */
  amrnbdec->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (amrnbdec->sinkpad, gst_amrnbdec_setcaps);
  gst_pad_set_event_function (amrnbdec->sinkpad, gst_amrnbdec_event);
  gst_pad_set_chain_function (amrnbdec->sinkpad, gst_amrnbdec_chain);
  gst_element_add_pad (GST_ELEMENT (amrnbdec), amrnbdec->sinkpad);

  /* create the src pad */
  amrnbdec->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (amrnbdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrnbdec), amrnbdec->srcpad);

  amrnbdec->adapter = gst_adapter_new ();

  /* init rest */
  amrnbdec->handle = NULL;
}

static void
gst_amrnbdec_finalize (GObject * object)
{
  GstAmrnbDec *amrnbdec;

  amrnbdec = GST_AMRNBDEC (object);

  gst_adapter_clear (amrnbdec->adapter);
  g_object_unref (amrnbdec->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_amrnbdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmrnbDec *self = GST_AMRNBDEC (object);

  switch (prop_id) {
    case PROP_VARIANT:
      self->variant = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_amrnbdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmrnbDec *self = GST_AMRNBDEC (object);

  switch (prop_id) {
    case PROP_VARIANT:
      g_value_set_enum (value, self->variant);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static gboolean
gst_amrnbdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrnbDec *amrnbdec;
  GstCaps *copy;

  amrnbdec = GST_AMRNBDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrnbdec->channels);
  gst_structure_get_int (structure, "rate", &amrnbdec->rate);

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/x-raw-int",
      "channels", G_TYPE_INT, amrnbdec->channels,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", G_TYPE_INT, amrnbdec->rate, "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  amrnbdec->duration = gst_util_uint64_scale_int (GST_SECOND, 160,
      amrnbdec->rate * amrnbdec->channels);

  gst_pad_set_caps (amrnbdec->srcpad, copy);
  gst_caps_unref (copy);

  gst_object_unref (amrnbdec);

  return TRUE;
}

static gboolean
gst_amrnbdec_event (GstPad * pad, GstEvent * event)
{
  GstAmrnbDec *amrnbdec;
  gboolean ret = TRUE;

  amrnbdec = GST_AMRNBDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (amrnbdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (amrnbdec->srcpad, event);
      gst_adapter_clear (amrnbdec->adapter);
      amrnbdec->ts = -1;
      break;
    case GST_EVENT_EOS:
      gst_adapter_clear (amrnbdec->adapter);
      ret = gst_pad_push_event (amrnbdec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need time for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (amrnbdec,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values */
      gst_segment_set_newsegment_full (&amrnbdec->segment, update,
          rate, arate, format, start, stop, time);
      ret = gst_pad_push_event (amrnbdec->srcpad, event);
    }
      break;
    default:
      ret = gst_pad_push_event (amrnbdec->srcpad, event);
      break;
  }
done:
  gst_object_unref (amrnbdec);

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (amrnbdec, "received non TIME newsegment");
    goto done;
  }
}

static GstFlowReturn
gst_amrnbdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrnbDec *amrnbdec;
  GstFlowReturn ret;

  amrnbdec = GST_AMRNBDEC (gst_pad_get_parent (pad));

  if (amrnbdec->rate == 0 || amrnbdec->channels == 0)
    goto not_negotiated;

  /* discontinuity, don't combine samples before and after the
   * DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (amrnbdec->adapter);
    amrnbdec->ts = -1;
    amrnbdec->discont = TRUE;
  }

  /* take latest timestamp, FIXME timestamp is the one of the
   * first buffer in the adapter. */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrnbdec->ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (amrnbdec->adapter, buffer);

  ret = GST_FLOW_OK;

  while (TRUE) {
    GstBuffer *out;
    guint8 *data;
    gint block, mode;

    /* need to peek data to get the size */
    if (gst_adapter_available (amrnbdec->adapter) < 1)
      break;
    data = (guint8 *) gst_adapter_peek (amrnbdec->adapter, 1);

    /* get size */
    switch (amrnbdec->variant) {
      case GST_AMRNB_VARIANT_IF1:
        mode = (data[0] >> 3) & 0x0F;
        block = block_size_if1[mode] + 1;
        break;
      case GST_AMRNB_VARIANT_IF2:
        mode = data[0] & 0x0F;
        block = block_size_if2[mode] + 1;
        break;
      default:
        goto invalid_variant;
    }

    GST_DEBUG_OBJECT (amrnbdec, "mode %d, block %d", mode, block);

    if (!block || gst_adapter_available (amrnbdec->adapter) < block)
      break;

    /* the library seems to write into the source data, hence
     * the copy. */
    data = gst_adapter_take (amrnbdec->adapter, block);

    /* get output */
    out = gst_buffer_new_and_alloc (160 * 2);
    GST_BUFFER_DURATION (out) = amrnbdec->duration;
    GST_BUFFER_TIMESTAMP (out) = amrnbdec->ts;

    if (amrnbdec->ts != -1)
      amrnbdec->ts += amrnbdec->duration;
    if (amrnbdec->discont) {
      GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
      amrnbdec->discont = FALSE;
    }

    gst_buffer_set_caps (out, GST_PAD_CAPS (amrnbdec->srcpad));

    /* decode */
    Decoder_Interface_Decode (amrnbdec->handle, data,
        (short *) GST_BUFFER_DATA (out), 0);
    g_free (data);

    /* send out */
    ret = gst_pad_push (amrnbdec->srcpad, out);
  }

  gst_object_unref (amrnbdec);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (amrnbdec, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Decoder is not initialized"));
    gst_object_unref (amrnbdec);
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_variant:
  {
    GST_ELEMENT_ERROR (amrnbdec, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Invalid variant"));
    gst_object_unref (amrnbdec);
    return GST_FLOW_ERROR;
  }
}

static GstStateChangeReturn
gst_amrnbdec_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrnbDec *amrnbdec;
  GstStateChangeReturn ret;

  amrnbdec = GST_AMRNBDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(amrnbdec->handle = Decoder_Interface_init ()))
        goto init_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (amrnbdec->adapter);
      amrnbdec->rate = 0;
      amrnbdec->channels = 0;
      amrnbdec->ts = -1;
      amrnbdec->discont = TRUE;
      gst_segment_init (&amrnbdec->segment, GST_FORMAT_TIME);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      Decoder_Interface_exit (amrnbdec->handle);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (amrnbdec, LIBRARY, INIT, (NULL),
        ("Failed to open AMR Decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }
}
