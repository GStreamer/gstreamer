/*
 * Siren Encoder Gst Element
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
 * SECTION:element-sirenenc
 *
 * This encodes audio buffers into the Siren 16 codec (a 16khz extension of
 * G.722.1) that is meant to be compatible with the Microsoft Windows Live
 * Messenger(tm) implementation.
 *
 * Ref: http://www.polycom.com/company/about_us/technology/siren_g7221/index.html
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsirenenc.h"

#include <string.h>

GST_DEBUG_CATEGORY (sirenenc_debug);
#define GST_CAT_DEFAULT (sirenenc_debug)

#define FRAME_DURATION  (20 * GST_MSECOND)

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-siren, " "dct-length = (int) 320"));

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
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



static void gst_siren_enc_finalize (GObject * object);

static gboolean gst_siren_enc_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_siren_enc_sink_event (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_siren_enc_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn
gst_siren_change_state (GstElement * element, GstStateChange transition);


static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (sirenenc_debug, "sirenenc", 0, "sirenenc");
}

GST_BOILERPLATE_FULL (GstSirenEnc, gst_siren_enc, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_siren_enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_details_simple (element_class, "Siren Encoder element",
      "Codec/Encoder/Audio ",
      "Encode 16bit PCM streams into the Siren7 codec",
      "Youness Alaoui <kakaroto@kakaroto.homelinux.net>");
}

static void
gst_siren_enc_class_init (GstSirenEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG ("Initializing Class");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_siren_enc_finalize);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_siren_change_state);

  GST_DEBUG ("Class Init done");
}

static void
gst_siren_enc_init (GstSirenEnc * enc, GstSirenEncClass * klass)
{

  GST_DEBUG_OBJECT (enc, "Initializing");
  enc->encoder = Siren7_NewEncoder (16000);
  enc->adapter = gst_adapter_new ();

  enc->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  enc->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");

  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_enc_sink_setcaps));
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_enc_sink_event));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_enc_chain));

  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  GST_DEBUG_OBJECT (enc, "Init done");
}

static void
gst_siren_enc_finalize (GObject * object)
{
  GstSirenEnc *enc = GST_SIREN_ENC (object);

  GST_DEBUG_OBJECT (object, "Disposing");

  Siren7_CloseEncoder (enc->encoder);
  g_object_unref (enc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_siren_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSirenEnc *enc;
  gboolean res;
  GstCaps *outcaps;

  enc = GST_SIREN_ENC (GST_PAD_PARENT (pad));

  outcaps = gst_static_pad_template_get_caps (&srctemplate);
  res = gst_pad_set_caps (enc->srcpad, outcaps);
  gst_caps_unref (outcaps);

  return res;
}

static gboolean
gst_siren_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstSirenEnc *enc;
  gboolean res;

  enc = GST_SIREN_ENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_adapter_clear (enc->adapter);
      res = gst_pad_push_event (enc->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (enc->adapter);
      res = gst_pad_push_event (enc->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (enc->srcpad, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_siren_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstSirenEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf;
  guint8 *in_data, *out_data;
  guint8 *to_free = NULL;
  guint i, size, num_frames;
  gint out_size, in_size;
  gint encode_ret;
  gboolean discont;
  GstClockTime timestamp;
  guint64 distance;
  GstCaps *outcaps;

  enc = GST_SIREN_ENC (GST_PAD_PARENT (pad));

  discont = GST_BUFFER_IS_DISCONT (buf);
  if (discont) {
    GST_DEBUG_OBJECT (enc, "received DISCONT, flush adapter");
    gst_adapter_clear (enc->adapter);
    enc->discont = TRUE;
  }

  gst_adapter_push (enc->adapter, buf);

  size = gst_adapter_available (enc->adapter);

  GST_LOG_OBJECT (enc, "Received buffer of size %d with adapter of size : %d",
      GST_BUFFER_SIZE (buf), size);

  /* we need to process 640 input bytes to produce 40 output bytes */
  /* calculate the amount of frames we will handle */
  num_frames = size / 640;

  /* no frames, wait some more */
  if (num_frames == 0)
    goto done;

  /* this is the input/output size */
  in_size = num_frames * 640;
  out_size = num_frames * 40;

  GST_LOG_OBJECT (enc, "we have %u frames, %u in, %u out", num_frames, in_size,
      out_size);

  /* set output caps when needed */
  if ((outcaps = GST_PAD_CAPS (enc->srcpad)) == NULL) {
    outcaps = gst_static_pad_template_get_caps (&srctemplate);
    gst_pad_set_caps (enc->srcpad, outcaps);
    gst_caps_unref (outcaps);
  }

  /* get a buffer */
  ret = gst_pad_alloc_buffer_and_set_caps (enc->srcpad, -1,
      out_size, outcaps, &out_buf);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  /* get the timestamp for the output buffer */
  timestamp = gst_adapter_prev_timestamp (enc->adapter, &distance);

  /* add the amount of time taken by the distance */
  if (timestamp != -1)
    timestamp += gst_util_uint64_scale_int (distance / 2, GST_SECOND, 16000);

  GST_LOG_OBJECT (enc,
      "timestamp %" GST_TIME_FORMAT ", distance %" G_GUINT64_FORMAT,
      GST_TIME_ARGS (timestamp), distance);

  /* get the input data for all the frames */
  to_free = in_data = gst_adapter_take (enc->adapter, in_size);
  out_data = GST_BUFFER_DATA (out_buf);

  for (i = 0; i < num_frames; i++) {
    GST_LOG_OBJECT (enc, "Encoding frame %u/%u", i, num_frames);

    /* encode 640 input bytes to 40 output bytes */
    encode_ret = Siren7_EncodeFrame (enc->encoder, in_data, out_data);
    if (encode_ret != 0)
      goto encode_error;

    /* move to next frame */
    out_data += 40;
    in_data += 640;
  }

  GST_LOG_OBJECT (enc, "Finished encoding");

  /* mark discont */
  if (enc->discont) {
    GST_BUFFER_FLAG_SET (out_buf, GST_BUFFER_FLAG_DISCONT);
    enc->discont = FALSE;
  }
  GST_BUFFER_TIMESTAMP (out_buf) = timestamp;
  GST_BUFFER_DURATION (out_buf) = num_frames * FRAME_DURATION;

  ret = gst_pad_push (enc->srcpad, out_buf);

done:
  if (to_free)
    g_free (to_free);

  return ret;

  /* ERRORS */
alloc_failed:
  {
    GST_DEBUG_OBJECT (enc, "failed to pad_alloc buffer: %d (%s)", ret,
        gst_flow_get_name (ret));
    goto done;
  }
encode_error:
  {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("Error encoding frame: %d", encode_ret));
    ret = GST_FLOW_ERROR;
    gst_buffer_unref (out_buf);
    goto done;
  }
}

static GstStateChangeReturn
gst_siren_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstSirenEnc *enc = GST_SIREN_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      enc->discont = FALSE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (enc->adapter);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_siren_enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "sirenenc",
      GST_RANK_MARGINAL, GST_TYPE_SIREN_ENC);
}
