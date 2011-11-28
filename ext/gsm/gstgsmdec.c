/*
 * Farsight
 * GStreamer GSM encoder
 * Copyright (C) 2005 Philippe Khalaf <burger@speedy.org>
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

#include "gstgsmdec.h"

GST_DEBUG_CATEGORY_STATIC (gsmdec_debug);
#define GST_CAT_DEFAULT (gsmdec_debug)

/* GSMDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  /* FILL ME */
  ARG_0
};

static void gst_gsmdec_base_init (gpointer g_class);
static void gst_gsmdec_class_init (GstGSMDec * klass);
static void gst_gsmdec_init (GstGSMDec * gsmdec);
static void gst_gsmdec_finalize (GObject * object);

static gboolean gst_gsmdec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_gsmdec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_gsmdec_chain (GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

/*static guint gst_gsmdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_gsmdec_get_type (void)
{
  static GType gsmdec_type = 0;

  if (!gsmdec_type) {
    static const GTypeInfo gsmdec_info = {
      sizeof (GstGSMDecClass),
      gst_gsmdec_base_init,
      NULL,
      (GClassInitFunc) gst_gsmdec_class_init,
      NULL,
      NULL,
      sizeof (GstGSMDec),
      0,
      (GInstanceInitFunc) gst_gsmdec_init,
    };

    gsmdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstGSMDec", &gsmdec_info, 0);
  }
  return gsmdec_type;
}

#define ENCODED_SAMPLES	160

static GstStaticPadTemplate gsmdec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, rate = (int) 8000, channels = (int) 1; "
        "audio/ms-gsm, rate = (int) [1, MAX], channels = (int) 1")
    );

static GstStaticPadTemplate gsmdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) [1, MAX], " "channels = (int) 1")
    );

static void
gst_gsmdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gsmdec_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gsmdec_src_template);
  gst_element_class_set_details_simple (element_class, "GSM audio decoder",
      "Codec/Decoder/Audio",
      "Decodes GSM encoded audio", "Philippe Khalaf <burger@speedy.org>");
}

static void
gst_gsmdec_class_init (GstGSMDec * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_gsmdec_finalize;

  GST_DEBUG_CATEGORY_INIT (gsmdec_debug, "gsmdec", 0, "GSM Decoder");
}

static void
gst_gsmdec_init (GstGSMDec * gsmdec)
{
  /* create the sink and src pads */
  gsmdec->sinkpad =
      gst_pad_new_from_static_template (&gsmdec_sink_template, "sink");
  gst_pad_set_setcaps_function (gsmdec->sinkpad, gst_gsmdec_sink_setcaps);
  gst_pad_set_event_function (gsmdec->sinkpad, gst_gsmdec_sink_event);
  gst_pad_set_chain_function (gsmdec->sinkpad, gst_gsmdec_chain);
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->sinkpad);

  gsmdec->srcpad =
      gst_pad_new_from_static_template (&gsmdec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->srcpad);

  gsmdec->state = gsm_create ();

  gsmdec->adapter = gst_adapter_new ();
  gsmdec->next_of = 0;
  gsmdec->next_ts = 0;
}

static void
gst_gsmdec_finalize (GObject * object)
{
  GstGSMDec *gsmdec;

  gsmdec = GST_GSMDEC (object);

  g_object_unref (gsmdec->adapter);
  gsm_destroy (gsmdec->state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_gsmdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGSMDec *gsmdec;
  GstCaps *srccaps;
  GstStructure *s;
  gboolean ret = FALSE;

  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));

  s = gst_caps_get_structure (caps, 0);
  if (s == NULL)
    goto wrong_caps;

  /* figure out if we deal with plain or MSGSM */
  if (gst_structure_has_name (s, "audio/x-gsm"))
    gsmdec->use_wav49 = 0;
  else if (gst_structure_has_name (s, "audio/ms-gsm"))
    gsmdec->use_wav49 = 1;
  else
    goto wrong_caps;

  if (!gst_structure_get_int (s, "rate", &gsmdec->rate)) {
    GST_WARNING_OBJECT (gsmdec, "missing sample rate parameter from sink caps");
    goto beach;
  }

  /* MSGSM needs different framing */
  gsm_option (gsmdec->state, GSM_OPT_WAV49, &gsmdec->use_wav49);

  gsmdec->duration = gst_util_uint64_scale (ENCODED_SAMPLES,
      GST_SECOND, gsmdec->rate);

  /* Setting up src caps based on the input sample rate. */
  srccaps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "rate", G_TYPE_INT, gsmdec->rate, "channels", G_TYPE_INT, 1, NULL);

  ret = gst_pad_set_caps (gsmdec->srcpad, srccaps);

  gst_caps_unref (srccaps);
  gst_object_unref (gsmdec);

  return ret;

  /* ERRORS */
wrong_caps:

  GST_ERROR_OBJECT (gsmdec, "invalid caps received");

beach:
  gst_object_unref (gsmdec);

  return ret;
}

static gboolean
gst_gsmdec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstGSMDec *gsmdec;

  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (gsmdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&gsmdec->segment, GST_FORMAT_UNDEFINED);
      res = gst_pad_push_event (gsmdec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* now configure the values */
      gst_segment_set_newsegment_full (&gsmdec->segment, update,
          rate, arate, format, start, stop, time);

      /* and forward */
      res = gst_pad_push_event (gsmdec->srcpad, event);
      break;
    }
    case GST_EVENT_EOS:
    default:
      res = gst_pad_push_event (gsmdec->srcpad, event);
      break;
  }

  gst_object_unref (gsmdec);

  return res;
}

static GstFlowReturn
gst_gsmdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstGSMDec *gsmdec;
  gsm_byte *data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime timestamp;
  gint needed;

  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (gsmdec->adapter);
    gsmdec->next_ts = GST_CLOCK_TIME_NONE;
    /* FIXME, do some good offset */
    gsmdec->next_of = 0;
  }
  gst_adapter_push (gsmdec->adapter, buf);

  needed = 33;
  /* do we have enough bytes to read a frame */
  while (gst_adapter_available (gsmdec->adapter) >= needed) {
    GstBuffer *outbuf;

    /* always the same amount of output samples */
    outbuf = gst_buffer_new_and_alloc (ENCODED_SAMPLES * sizeof (gsm_signal));

    /* If we are not given any timestamp, interpolate from last seen
     * timestamp (if any). */
    if (timestamp == GST_CLOCK_TIME_NONE)
      timestamp = gsmdec->next_ts;

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    /* interpolate in the next run */
    if (timestamp != GST_CLOCK_TIME_NONE)
      gsmdec->next_ts = timestamp + gsmdec->duration;
    timestamp = GST_CLOCK_TIME_NONE;

    GST_BUFFER_DURATION (outbuf) = gsmdec->duration;
    GST_BUFFER_OFFSET (outbuf) = gsmdec->next_of;
    if (gsmdec->next_of != -1)
      gsmdec->next_of += ENCODED_SAMPLES;
    GST_BUFFER_OFFSET_END (outbuf) = gsmdec->next_of;

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (gsmdec->srcpad));

    /* now encode frame into the output buffer */
    data = (gsm_byte *) gst_adapter_peek (gsmdec->adapter, needed);
    if (gsm_decode (gsmdec->state, data,
            (gsm_signal *) GST_BUFFER_DATA (outbuf)) < 0) {
      /* invalid frame */
      GST_WARNING_OBJECT (gsmdec, "tried to decode an invalid frame, skipping");
    }
    gst_adapter_flush (gsmdec->adapter, needed);

    /* WAV49 requires alternating 33 and 32 bytes of input */
    if (gsmdec->use_wav49)
      needed = (needed == 33 ? 32 : 33);

    GST_DEBUG_OBJECT (gsmdec, "Pushing buffer of size %d ts %" GST_TIME_FORMAT,
        GST_BUFFER_SIZE (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

    /* push */
    ret = gst_pad_push (gsmdec->srcpad, outbuf);
  }

  gst_object_unref (gsmdec);

  return ret;
}
