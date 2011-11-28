/*
 * Siren Decoder Gst Element
 *
 *   @author: Youness Alaoui <kakaroto@kakaroto.homelinux.net>
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
 *
 */
/**
 * SECTION:element-sirendec
 *
 * This decodes audio buffers from the Siren 16 codec (a 16khz extension of
 * G.722.1) that is meant to be compatible with the Microsoft Windows Live
 * Messenger(tm) implementation.
 *
 * Ref: http://www.polycom.com/company/about_us/technology/siren_g7221/index.html
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsirendec.h"

#include <string.h>

GST_DEBUG_CATEGORY (sirendec_debug);
#define GST_CAT_DEFAULT (sirendec_debug)

#define FRAME_DURATION  (20 * GST_MSECOND)

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-siren, " "dct-length = (int) 320"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) 1234, "
        "signed = (boolean) true, "
        "rate = (int) 16000, " "channels = (int) 1"));

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static void gst_siren_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_siren_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_siren_dec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_siren_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_siren_dec_chain (GstPad * pad, GstBuffer * buf);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (sirendec_debug, "sirendec", 0, "sirendec");
}

GST_BOILERPLATE_FULL (GstSirenDec, gst_siren_dec, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_siren_dec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_details_simple (element_class, "Siren Decoder element",
      "Codec/Decoder/Audio ",
      "Decode streams encoded with the Siren7 codec into 16bit PCM",
      "Youness Alaoui <kakaroto@kakaroto.homelinux.net>");
}

static void
gst_siren_dec_class_init (GstSirenDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG ("Initializing Class");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_siren_dec_finalize);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_siren_change_state);

  GST_DEBUG ("Class Init done");
}

static void
gst_siren_dec_init (GstSirenDec * dec, GstSirenDecClass * klass)
{

  GST_DEBUG_OBJECT (dec, "Initializing");
  dec->decoder = Siren7_NewDecoder (16000);;

  dec->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  dec->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");

  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_dec_sink_setcaps));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_dec_sink_event));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_dec_chain));

  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->adapter = gst_adapter_new ();

  GST_DEBUG_OBJECT (dec, "Init done");
}

static void
gst_siren_dec_finalize (GObject * object)
{
  GstSirenDec *dec = GST_SIREN_DEC (object);

  GST_DEBUG_OBJECT (dec, "Finalize");

  Siren7_CloseDecoder (dec->decoder);
  g_object_unref (dec->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_siren_dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSirenDec *dec;
  gboolean res;
  GstCaps *outcaps;

  dec = GST_SIREN_DEC (GST_PAD_PARENT (pad));

  outcaps = gst_static_pad_template_get_caps (&srctemplate);
  res = gst_pad_set_caps (dec->srcpad, outcaps);
  gst_caps_unref (outcaps);

  return res;
}

static gboolean
gst_siren_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstSirenDec *dec;
  gboolean res;

  dec = GST_SIREN_DEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_adapter_clear (dec->adapter);
      res = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (dec->adapter);
      res = gst_pad_push_event (dec->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (dec->srcpad, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_siren_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstSirenDec *dec;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf;
  guint8 *in_data, *out_data;
  guint8 *to_free = NULL;
  guint i, size, num_frames;
  gint out_size, in_size;
  gint decode_ret;
  gboolean discont;
  GstClockTime timestamp;
  guint64 distance;
  GstCaps *outcaps;

  dec = GST_SIREN_DEC (GST_PAD_PARENT (pad));

  discont = GST_BUFFER_IS_DISCONT (buf);
  if (discont) {
    GST_DEBUG_OBJECT (dec, "received DISCONT, flush adapter");
    gst_adapter_clear (dec->adapter);
    dec->discont = TRUE;
  }

  gst_adapter_push (dec->adapter, buf);

  size = gst_adapter_available (dec->adapter);

  GST_LOG_OBJECT (dec, "Received buffer of size %u with adapter of size : %u",
      GST_BUFFER_SIZE (buf), size);

  /* process 40 input bytes into 640 output bytes */
  num_frames = size / 40;

  if (num_frames == 0)
    goto done;

  /* this is the input/output size */
  in_size = num_frames * 40;
  out_size = num_frames * 640;

  GST_LOG_OBJECT (dec, "we have %u frames, %u in, %u out", num_frames, in_size,
      out_size);

  /* set output caps when needed */
  if ((outcaps = GST_PAD_CAPS (dec->srcpad)) == NULL) {
    outcaps = gst_static_pad_template_get_caps (&srctemplate);
    gst_pad_set_caps (dec->srcpad, outcaps);
    gst_caps_unref (outcaps);
  }

  /* get a buffer */
  ret = gst_pad_alloc_buffer_and_set_caps (dec->srcpad, -1,
      out_size, outcaps, &out_buf);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  /* get the timestamp for the output buffer */
  timestamp = gst_adapter_prev_timestamp (dec->adapter, &distance);

  /* add the amount of time taken by the distance, each frame is 20ms */
  if (timestamp != -1)
    timestamp += (distance / 40) * FRAME_DURATION;

  GST_LOG_OBJECT (dec,
      "timestamp %" GST_TIME_FORMAT ", distance %" G_GUINT64_FORMAT,
      GST_TIME_ARGS (timestamp), distance);

  /* get the input data for all the frames */
  to_free = in_data = gst_adapter_take (dec->adapter, in_size);
  out_data = GST_BUFFER_DATA (out_buf);

  for (i = 0; i < num_frames; i++) {
    GST_LOG_OBJECT (dec, "Decoding frame %u/%u", i, num_frames);

    /* decode 40 input bytes to 640 output bytes */
    decode_ret = Siren7_DecodeFrame (dec->decoder, in_data, out_data);
    if (decode_ret != 0)
      goto decode_error;

    /* move to next frame */
    out_data += 640;
    in_data += 40;
  }

  GST_LOG_OBJECT (dec, "Finished decoding");

  /* mark discont */
  if (dec->discont) {
    GST_BUFFER_FLAG_SET (out_buf, GST_BUFFER_FLAG_DISCONT);
    dec->discont = FALSE;
  }

  GST_BUFFER_TIMESTAMP (out_buf) = timestamp;
  GST_BUFFER_DURATION (out_buf) = num_frames * FRAME_DURATION;

  ret = gst_pad_push (dec->srcpad, out_buf);

done:
  if (to_free)
    g_free (to_free);

  return ret;

  /* ERRORS */
alloc_failed:
  {
    GST_DEBUG_OBJECT (dec, "failed to pad_alloc buffer: %d (%s)", ret,
        gst_flow_get_name (ret));
    goto done;
  }
decode_error:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
        ("Error decoding frame: %d", decode_ret));
    ret = GST_FLOW_ERROR;
    gst_buffer_unref (out_buf);
    goto done;
  }
}

static GstStateChangeReturn
gst_siren_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstSirenDec *dec = GST_SIREN_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dec->discont = FALSE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (dec->adapter);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_siren_dec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "sirendec",
      GST_RANK_MARGINAL, GST_TYPE_SIREN_DEC);
}
