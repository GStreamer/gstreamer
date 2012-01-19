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

#include "gstgsmenc.h"

GST_DEBUG_CATEGORY_STATIC (gsmenc_debug);
#define GST_CAT_DEFAULT (gsmenc_debug)

/* GSMEnc signals and args */
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

static gboolean gst_gsmenc_start (GstAudioEncoder * enc);
static gboolean gst_gsmenc_stop (GstAudioEncoder * enc);
static gboolean gst_gsmenc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_gsmenc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);

static GstStaticPadTemplate gsmenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate gsmenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 8000, " "channels = (int) 1")
    );

GST_BOILERPLATE (GstGSMEnc, gst_gsmenc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER);

static void
gst_gsmenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gsmenc_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gsmenc_src_template);
  gst_element_class_set_details_simple (element_class, "GSM audio encoder",
      "Codec/Encoder/Audio",
      "Encodes GSM audio", "Philippe Khalaf <burger@speedy.org>");
}

static void
gst_gsmenc_class_init (GstGSMEncClass * klass)
{
  GstAudioEncoderClass *base_class;

  base_class = (GstAudioEncoderClass *) klass;

  base_class->start = GST_DEBUG_FUNCPTR (gst_gsmenc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_gsmenc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_gsmenc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_gsmenc_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gsmenc_debug, "gsmenc", 0, "GSM Encoder");
}

static void
gst_gsmenc_init (GstGSMEnc * gsmenc, GstGSMEncClass * klass)
{
}

static gboolean
gst_gsmenc_start (GstAudioEncoder * enc)
{
  GstGSMEnc *gsmenc = GST_GSMENC (enc);
  gint use_wav49;

  GST_DEBUG_OBJECT (enc, "start");

  gsmenc->state = gsm_create ();

  /* turn off WAV49 handling */
  use_wav49 = 0;
  gsm_option (gsmenc->state, GSM_OPT_WAV49, &use_wav49);

  return TRUE;
}

static gboolean
gst_gsmenc_stop (GstAudioEncoder * enc)
{
  GstGSMEnc *gsmenc = GST_GSMENC (enc);

  GST_DEBUG_OBJECT (enc, "stop");
  gsm_destroy (gsmenc->state);

  return TRUE;
}

static gboolean
gst_gsmenc_set_format (GstAudioEncoder * benc, GstAudioInfo * info)
{
  GstCaps *srccaps;

  srccaps = gst_static_pad_template_get_caps (&gsmenc_src_template);
  gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (benc), srccaps);

  /* report needs to base class */
  gst_audio_encoder_set_frame_samples_min (benc, 160);
  gst_audio_encoder_set_frame_samples_max (benc, 160);
  gst_audio_encoder_set_frame_max (benc, 1);

  return TRUE;
}

static GstFlowReturn
gst_gsmenc_handle_frame (GstAudioEncoder * benc, GstBuffer * buffer)
{
  GstGSMEnc *gsmenc;
  gsm_signal *data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;

  gsmenc = GST_GSMENC (benc);

  /* we don't deal with squeezing remnants, so simply discard those */
  if (G_UNLIKELY (buffer == NULL)) {
    GST_DEBUG_OBJECT (gsmenc, "no data");
    goto done;
  }

  if (G_UNLIKELY (GST_BUFFER_SIZE (buffer) < 320)) {
    GST_DEBUG_OBJECT (gsmenc, "discarding trailing data %d",
        GST_BUFFER_SIZE (buffer));
    ret = gst_audio_encoder_finish_frame (benc, NULL, -1);
    goto done;
  }

  outbuf = gst_buffer_new_and_alloc (33 * sizeof (gsm_byte));

  /* encode 160 16-bit samples into 33 bytes */
  data = (gsm_signal *) GST_BUFFER_DATA (buffer);
  gsm_encode (gsmenc->state, data, (gsm_byte *) GST_BUFFER_DATA (outbuf));

  GST_LOG_OBJECT (gsmenc, "encoded to %d bytes", GST_BUFFER_SIZE (outbuf));

  ret = gst_audio_encoder_finish_frame (benc, outbuf, 160);

done:
  return ret;
}
