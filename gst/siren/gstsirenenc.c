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

static gboolean gst_siren_enc_start (GstAudioEncoder * enc);
static gboolean gst_siren_enc_stop (GstAudioEncoder * enc);
static gboolean gst_siren_enc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_siren_enc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (sirenenc_debug, "sirenenc", 0, "sirenenc");
}

GST_BOILERPLATE_FULL (GstSirenEnc, gst_siren_enc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER, _do_init);

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
  GstAudioEncoderClass *base_class = GST_AUDIO_ENCODER_CLASS (klass);

  GST_DEBUG ("Initializing Class");

  base_class->start = GST_DEBUG_FUNCPTR (gst_siren_enc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_siren_enc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_siren_enc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_siren_enc_handle_frame);

  GST_DEBUG ("Class Init done");
}

static void
gst_siren_enc_init (GstSirenEnc * enc, GstSirenEncClass * klass)
{
}

static gboolean
gst_siren_enc_start (GstAudioEncoder * enc)
{
  GstSirenEnc *senc = GST_SIREN_ENC (enc);

  GST_DEBUG_OBJECT (enc, "start");

  senc->encoder = Siren7_NewEncoder (16000);

  return TRUE;
}

static gboolean
gst_siren_enc_stop (GstAudioEncoder * enc)
{
  GstSirenEnc *senc = GST_SIREN_ENC (enc);

  GST_DEBUG_OBJECT (senc, "stop");

  Siren7_CloseEncoder (senc->encoder);

  return TRUE;
}

static gboolean
gst_siren_enc_set_format (GstAudioEncoder * benc, GstAudioInfo * info)
{
  GstSirenEnc *enc;
  gboolean res;
  GstCaps *outcaps;

  enc = GST_SIREN_ENC (benc);

  outcaps = gst_static_pad_template_get_caps (&srctemplate);
  res = gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc), outcaps);
  gst_caps_unref (outcaps);

  /* report needs to base class */
  gst_audio_encoder_set_frame_samples_min (benc, 320);
  gst_audio_encoder_set_frame_samples_max (benc, 320);
  /* no remainder or flushing please */
  gst_audio_encoder_set_hard_min (benc, TRUE);
  gst_audio_encoder_set_drainable (benc, FALSE);

  return res;
}

static GstFlowReturn
gst_siren_enc_handle_frame (GstAudioEncoder * benc, GstBuffer * buf)
{
  GstSirenEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf;
  guint8 *in_data, *out_data;
  guint i, size, num_frames;
  gint out_size, in_size;
  gint encode_ret;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  enc = GST_SIREN_ENC (benc);

  size = GST_BUFFER_SIZE (buf);

  GST_LOG_OBJECT (enc, "Received buffer of size %d", GST_BUFFER_SIZE (buf));

  g_return_val_if_fail (size > 0, GST_FLOW_ERROR);
  g_return_val_if_fail (size % 640 == 0, GST_FLOW_ERROR);

  /* we need to process 640 input bytes to produce 40 output bytes */
  /* calculate the amount of frames we will handle */
  num_frames = size / 640;

  /* this is the input/output size */
  in_size = num_frames * 640;
  out_size = num_frames * 40;

  GST_LOG_OBJECT (enc, "we have %u frames, %u in, %u out", num_frames, in_size,
      out_size);

  /* get a buffer */
  ret = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_ENCODER_SRC_PAD (benc),
      -1, out_size, GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (benc)), &out_buf);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  /* get the input data for all the frames */
  in_data = GST_BUFFER_DATA (buf);
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

  /* we encode all we get, pass it along */
  ret = gst_audio_encoder_finish_frame (benc, out_buf, -1);

done:
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

gboolean
gst_siren_enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "sirenenc",
      GST_RANK_MARGINAL, GST_TYPE_SIREN_ENC);
}
