/* GStreamer
 * Copyright (C) <2002> Iain Holmes <iain@prettypeople.org>
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

#include <string.h>
#include <gstwavenc.h>

static void 	gst_wavenc_class_init 	(GstWavEncClass *klass);
static void 	gst_wavenc_init 	(GstWavEnc *wavenc);
static void 	gst_wavenc_chain 	(GstPad *pad, GstBuffer *buf);

#define WAVE_FORMAT_PCM 0x0001

#define WRITE_U32(buf, x) *(buf) = (unsigned char) (x&0xff);\
*((buf)+1) = (unsigned char)((x>>8)&0xff);\
*((buf)+2) = (unsigned char)((x>>16)&0xff);\
*((buf)+3) = (unsigned char)((x>>24)&0xff);

#define WRITE_U16(buf, x) *(buf) = (unsigned char) (x&0xff);\
*((buf)+1) = (unsigned char)((x>>8)&0xff);

struct riff_struct {
  guint8 	id[4]; 		/* RIFF */
  guint32 	len;
  guint8	wav_id[4]; 	/* WAVE */
};

struct chunk_struct {
  guint8 	id[4];
  guint32  	len;
};

struct common_struct {
  guint16 	wFormatTag;
  guint16 	wChannels;
  guint32	dwSamplesPerSec;
  guint32	dwAvgBytesPerSec;
  guint16 	wBlockAlign;
  guint16 	wBitsPerSample; 	/* Only for PCM */
};

struct wave_header {
  struct riff_struct 	riff;
  struct chunk_struct 	format;
  struct common_struct 	common;
  struct chunk_struct 	data;
};

static GstElementDetails gst_wavenc_details = {
  "WAV encoder",
  "Codec/Encoder",
  "LGPL",
  "Encode raw audio into WAV",
  VERSION,
  "Iain Holmes <iain@prettypeople.org>",
  "(C) 2002",
};

static GstPadTemplate *srctemplate, *sinktemplate;

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "wavenc_raw",
    "audio/raw",
      "format",   GST_PROPS_STRING ("int"),
      "law",         GST_PROPS_INT (0),
      "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
      "signed",      GST_PROPS_BOOLEAN (TRUE),
      "width",       GST_PROPS_INT (16),
      "depth",       GST_PROPS_INT (16),
      "rate",        GST_PROPS_INT_RANGE (8000, 48000),
      "channels",    GST_PROPS_INT_RANGE (1, 2)
  )
)

GST_PAD_TEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "wavenc_wav",
    "audio/x-wav",
    NULL
  )
)

static GstElementClass *parent_class = NULL;

static GType
gst_wavenc_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GstWavEncClass), 
      NULL, 
      NULL,
      (GClassInitFunc) gst_wavenc_class_init, 
      NULL, 
      NULL,
      sizeof (GstWavEnc), 
      0, 
      (GInstanceInitFunc) gst_wavenc_init
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstWavEnc", &info, 0);
  }

  return type;
}

static GstElementStateReturn
gst_wavenc_change_state (GstElement *element)
{
  GstWavEnc *wavenc = GST_WAVENC (element);
  
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      wavenc->setup = FALSE;
      wavenc->flush_header = TRUE;
      break;
    default:
      break;
  }

  if (parent_class->change_state) {
    return parent_class->change_state (element);
  }

  return GST_STATE_SUCCESS;
}

static void
gst_wavenc_class_init (GstWavEncClass *klass)
{
  GstElementClass *element_class;

  element_class = (GstElementClass *) klass;
  element_class->change_state = gst_wavenc_change_state;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static gboolean
gst_wavenc_setup (GstWavEnc *wavenc)
{
  struct wave_header wave;
  gint size = 0x7fffffff; /* Use a bogus size initially */

  wave.common.wChannels = wavenc->channels;
  wave.common.wBitsPerSample = wavenc->bits;
  wave.common.dwSamplesPerSec = wavenc->rate;

  memset (wavenc->header, 0, WAV_HEADER_LEN);

  /* Fill out our wav-header with some information */
  strncpy (wave.riff.id, "RIFF", 4);
  wave.riff.len = size - 8;
  strncpy (wave.riff.wav_id, "WAVE", 4);

  strncpy (wave.format.id, "fmt ", 4);
  wave.format.len = 16;

  wave.common.wFormatTag = WAVE_FORMAT_PCM;
  wave.common.dwAvgBytesPerSec = wave.common.wChannels * wave.common.dwSamplesPerSec * (wave.common.wBitsPerSample >> 3);
  wave.common.wBlockAlign = wave.common.wChannels * (wave.common.wBitsPerSample >> 3);

  strncpy (wave.data.id, "data", 4);
  wave.data.len = size - 44;

  strncpy (wavenc->header, wave.riff.id, 4);
  WRITE_U32 (wavenc->header + 4, wave.riff.len);
  strncpy (wavenc->header + 8, wave.riff.wav_id, 4);
  strncpy (wavenc->header + 12, wave.format.id, 4);
  WRITE_U32 (wavenc->header + 16, wave.format.len);
  WRITE_U16 (wavenc->header + 20, wave.common.wFormatTag);
  WRITE_U16 (wavenc->header + 22, wave.common.wChannels);
  WRITE_U32 (wavenc->header + 24, wave.common.dwSamplesPerSec);
  WRITE_U32 (wavenc->header + 28, wave.common.dwAvgBytesPerSec);
  WRITE_U16 (wavenc->header + 32, wave.common.wBlockAlign);
  WRITE_U16 (wavenc->header + 34, wave.common.wBitsPerSample);
  strncpy (wavenc->header + 36, wave.data.id, 4);
  WRITE_U32 (wavenc->header + 40, wave.data.len);

  wavenc->setup = TRUE;
  return TRUE;
}

static GstPadConnectReturn
gst_wavenc_sinkconnect (GstPad *pad,
			GstCaps *caps)
{
  GstWavEnc *wavenc;

  wavenc = GST_WAVENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_caps_get_int (caps, "channels", &wavenc->channels);
  gst_caps_get_int (caps, "rate", &wavenc->rate);
  gst_caps_get_int (caps, "depth", &wavenc->bits);

  gst_wavenc_setup (wavenc);

  if (wavenc->setup) {
    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
}

static void
gst_wavenc_init (GstWavEnc *wavenc)
{
  wavenc->sinkpad = gst_pad_new_from_template (sinktemplate, "sink");
  gst_element_add_pad (GST_ELEMENT (wavenc), wavenc->sinkpad);
  gst_pad_set_chain_function (wavenc->sinkpad, gst_wavenc_chain);
  gst_pad_set_link_function (wavenc->sinkpad, gst_wavenc_sinkconnect);

  wavenc->srcpad = gst_pad_new_from_template (srctemplate, "src");
  gst_element_add_pad (GST_ELEMENT (wavenc), wavenc->srcpad);

  wavenc->setup = FALSE;
  wavenc->flush_header = TRUE;
}

static void
gst_wavenc_chain (GstPad *pad,
		  GstBuffer *buf)
{
  GstWavEnc *wavenc;

  wavenc = GST_WAVENC (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        /* Should do something... */
        gst_event_unref (event);

        if (GST_PAD_IS_USABLE (wavenc->srcpad))
          gst_pad_push (wavenc->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
        gst_element_set_eos (GST_ELEMENT (wavenc));
        break;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
  } 
  else {
    if (!wavenc->setup) {
      gst_buffer_unref (buf);
      gst_element_error (GST_ELEMENT (wavenc), "encoder not initialised (input is not audio?)");
      return;
    }

    if (GST_PAD_IS_USABLE (wavenc->srcpad)) {
      if (wavenc->flush_header) {
        GstBuffer *outbuf;

        outbuf = gst_buffer_new_and_alloc (WAV_HEADER_LEN);
        memcpy (GST_BUFFER_DATA (outbuf), wavenc->header, WAV_HEADER_LEN);

	gst_pad_push (wavenc->srcpad, outbuf);
        wavenc->flush_header = FALSE;
      }
    
      gst_pad_push (wavenc->srcpad, buf);
    }
  }
}

static gboolean
plugin_init (GModule *module,
	     GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new ("wavenc", GST_TYPE_WAVENC,
				     &gst_wavenc_details);
  
  srctemplate = src_factory ();
  gst_element_factory_add_pad_template (factory, srctemplate);

  sinktemplate = sink_factory ();
  gst_element_factory_add_pad_template (factory, sinktemplate);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "wavenc",
  plugin_init
};
    
