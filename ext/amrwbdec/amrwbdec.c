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
 * SECTION:element-amrwbdec
 * @see_also: #GstAmrwbEnc
 *
 * AMR wideband decoder based on the 
 * <ulink url="http://sourceforge.net/projects/opencore-amr">opencore codec implementation</ulink>.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.amr ! amrparse ! amrwbdec ! audioresample ! audioconvert ! alsasink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "amrwbdec.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR-WB, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

GST_DEBUG_CATEGORY_STATIC (gst_amrwbdec_debug);
#define GST_CAT_DEFAULT gst_amrwbdec_debug

#define L_FRAME16k      320     /* Frame size at 16kHz  */

static const unsigned char block_size[16] =
    { 18, 24, 33, 37, 41, 47, 51, 59, 61,
  6, 0, 0, 0, 0, 1, 1
};

static gboolean gst_amrwbdec_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_amrwbdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_amrwbdec_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_amrwbdec_state_change (GstElement * element,
    GstStateChange transition);

static void gst_amrwbdec_finalize (GObject * object);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_amrwbdec_debug, "amrwbdec", 0, "AMR-WB audio decoder");

GST_BOILERPLATE_FULL (GstAmrwbDec, gst_amrwbdec, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_amrwbdec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class, "AMR-WB audio decoder",
      "Codec/Decoder/Audio",
      "Adaptive Multi-Rate Wideband audio decoder",
      "Renato Araujo <renato.filho@indt.org.br>");
}

static void
gst_amrwbdec_class_init (GstAmrwbDecClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->finalize = gst_amrwbdec_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_amrwbdec_state_change);
}

static void
gst_amrwbdec_init (GstAmrwbDec * amrwbdec, GstAmrwbDecClass * klass)
{
  /* create the sink pad */
  amrwbdec->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (amrwbdec->sinkpad, gst_amrwbdec_setcaps);
  gst_pad_set_event_function (amrwbdec->sinkpad, gst_amrwbdec_event);
  gst_pad_set_chain_function (amrwbdec->sinkpad, gst_amrwbdec_chain);
  gst_element_add_pad (GST_ELEMENT (amrwbdec), amrwbdec->sinkpad);

  /* create the src pad */
  amrwbdec->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (amrwbdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrwbdec), amrwbdec->srcpad);

  amrwbdec->adapter = gst_adapter_new ();

  /* init rest */
  amrwbdec->handle = NULL;
  amrwbdec->channels = 0;
  amrwbdec->rate = 0;
  amrwbdec->duration = 0;
  amrwbdec->ts = -1;
}

static void
gst_amrwbdec_finalize (GObject * object)
{
  GstAmrwbDec *amrwbdec;

  amrwbdec = GST_AMRWBDEC (object);

  gst_adapter_clear (amrwbdec->adapter);
  g_object_unref (amrwbdec->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_amrwbdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrwbDec *amrwbdec;
  GstCaps *copy;

  amrwbdec = GST_AMRWBDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrwbdec->channels);
  gst_structure_get_int (structure, "rate", &amrwbdec->rate);

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/x-raw-int",
      "channels", G_TYPE_INT, amrwbdec->channels,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", G_TYPE_INT, amrwbdec->rate, "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  amrwbdec->duration = gst_util_uint64_scale_int (GST_SECOND, L_FRAME16k,
      amrwbdec->rate * amrwbdec->channels);

  gst_pad_set_caps (amrwbdec->srcpad, copy);
  gst_caps_unref (copy);

  gst_object_unref (amrwbdec);

  return TRUE;
}

static gboolean
gst_amrwbdec_event (GstPad * pad, GstEvent * event)
{
  GstAmrwbDec *amrwbdec;
  gboolean ret = TRUE;

  amrwbdec = GST_AMRWBDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
      gst_adapter_clear (amrwbdec->adapter);
      amrwbdec->ts = -1;
      break;
    case GST_EVENT_EOS:
      gst_adapter_clear (amrwbdec->adapter);
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
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

      GST_DEBUG_OBJECT (amrwbdec,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values */
      gst_segment_set_newsegment_full (&amrwbdec->segment, update,
          rate, arate, format, start, stop, time);
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
    }
      break;
    default:
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
      break;
  }
done:
  gst_object_unref (amrwbdec);

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (amrwbdec, "received non TIME newsegment");
    goto done;
  }
}

static GstFlowReturn
gst_amrwbdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrwbDec *amrwbdec;
  GstFlowReturn ret = GST_FLOW_OK;

  amrwbdec = GST_AMRWBDEC (gst_pad_get_parent (pad));

  if (amrwbdec->rate == 0 || amrwbdec->channels == 0)
    goto not_negotiated;

  /* discontinuity, don't combine samples before and after the
   * DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (amrwbdec->adapter);
    amrwbdec->ts = -1;
    amrwbdec->discont = TRUE;
  }

  /* take latest timestamp, FIXME timestamp is the one of the
   * first buffer in the adapter. */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrwbdec->ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (amrwbdec->adapter, buffer);

  while (TRUE) {
    GstBuffer *out;
    const guint8 *data;
    gint block, mode;

    /* need to peek data to get the size */
    if (gst_adapter_available (amrwbdec->adapter) < 1)
      break;
    data = gst_adapter_peek (amrwbdec->adapter, 1);

    /* get size */
    mode = (data[0] >> 3) & 0x0F;
    block = block_size[mode];

    GST_DEBUG_OBJECT (amrwbdec, "mode %d, block %d", mode, block);

    if (!block) {
      GST_LOG_OBJECT (amrwbdec, "skipping byte");
      gst_adapter_flush (amrwbdec->adapter, 1);
      continue;
    }

    if (gst_adapter_available (amrwbdec->adapter) < block)
      break;

    /* the library seems to write into the source data, hence the copy. */
    data = gst_adapter_take (amrwbdec->adapter, block);

    /* get output */
    out = gst_buffer_new_and_alloc (sizeof (gint16) * L_FRAME16k);

    GST_BUFFER_DURATION (out) = amrwbdec->duration;
    GST_BUFFER_TIMESTAMP (out) = amrwbdec->ts;

    if (amrwbdec->ts != -1)
      amrwbdec->ts += amrwbdec->duration;
    if (amrwbdec->discont) {
      GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
      amrwbdec->discont = FALSE;
    }

    gst_buffer_set_caps (out, GST_PAD_CAPS (amrwbdec->srcpad));

    /* decode */
    D_IF_decode (amrwbdec->handle, (unsigned char *) data,
        (Word16 *) GST_BUFFER_DATA (out), _good_frame);

    g_free ((gpointer) data);

    /* send out */
    ret = gst_pad_push (amrwbdec->srcpad, out);
  }

  gst_object_unref (amrwbdec);
  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (amrwbdec, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Decoder is not initialized"));
    gst_object_unref (amrwbdec);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstStateChangeReturn
gst_amrwbdec_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrwbDec *amrwbdec;
  GstStateChangeReturn ret;

  amrwbdec = GST_AMRWBDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(amrwbdec->handle = D_IF_init ()))
        goto init_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (amrwbdec->adapter);
      amrwbdec->rate = 0;
      amrwbdec->channels = 0;
      amrwbdec->ts = -1;
      amrwbdec->discont = TRUE;
      gst_segment_init (&amrwbdec->segment, GST_FORMAT_TIME);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      D_IF_exit (amrwbdec->handle);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (amrwbdec, LIBRARY, INIT, (NULL),
        ("Failed to open AMR Decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }
}
