/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/*#define GST_DEBUG_ENABLED */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gstmpegaudioparse.h>


/* elementfactory information */
static GstElementDetails mp3parse_details = {
  "MPEG1 Audio Parser",
  "Codec/Parser/Audio",
  "Parses and frames mpeg1 audio streams (levels 1-3), provides seek",
  "Erik Walthinsen <omega@cse.ogi.edu>"
};

static GstStaticPadTemplate mp3_src_template =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
      "mpegversion = (int) 1, "
      "layer = (int) [ 1, 3 ], "
      "rate = (int) [ 8000, 48000], "
      "channels = (int) [ 1, 2 ]")
);

static GstStaticPadTemplate mp3_sink_template =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
      "mpegversion = (int) 1"
    )
);

/* GstMPEGAudioParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SKIP,
  ARG_BIT_RATE,
  /* FILL ME */
};


static void	gst_mp3parse_class_init		(GstMPEGAudioParseClass *klass);
static void	gst_mp3parse_base_init		(GstMPEGAudioParseClass *klass);
static void	gst_mp3parse_init		(GstMPEGAudioParse *mp3parse);

static void	gst_mp3parse_chain		(GstPad *pad,GstData *_data);
static long	bpf_from_header			(GstMPEGAudioParse *parse, unsigned long header);
static int	head_check			(unsigned long head);

static void	gst_mp3parse_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_mp3parse_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstElementStateReturn
		gst_mp3parse_change_state	(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_mp3parse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mp3parse_get_type(void) {
  static GType mp3parse_type = 0;

  if (!mp3parse_type) {
    static const GTypeInfo mp3parse_info = {
      sizeof(GstMPEGAudioParseClass),
      (GBaseInitFunc)gst_mp3parse_base_init,
      NULL,
      (GClassInitFunc)gst_mp3parse_class_init,
      NULL,
      NULL,
      sizeof(GstMPEGAudioParse),
      0,
      (GInstanceInitFunc)gst_mp3parse_init,
    };
    mp3parse_type = g_type_register_static (GST_TYPE_ELEMENT,
					    "GstMPEGAudioParse",
					    &mp3parse_info, 0);
  }
  return mp3parse_type;
}

static guint mp3types_bitrates[2][3][16] =
{ { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, },
    {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, },
    {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, } },
  { {0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, },
    {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, },
    {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, } },
};

static guint mp3types_freqs[3][3] =
{ {44100, 48000, 32000},
  {22050, 24000, 16000}, 
  {11025, 12000,  8000}};

static inline guint
mp3_type_frame_length_from_header (guint32 header, guint *put_layer,
				   guint *put_channels, guint *put_bitrate,
				   guint *put_samplerate)
{
  guint length;
  gulong mode, samplerate, bitrate, layer, channels, padding;
  gint lsf, mpg25;

  if (header & (1 << 20)) {
    lsf = (header & (1 << 19)) ? 0 : 1;
    mpg25 = 0;
  } else {
    lsf = 1;
    mpg25 = 1;
  }

  mode = (header >> 6) & 0x3;
  channels = (mode == 3) ? 1 : 2;
  samplerate = (header >> 10) & 0x3;
  samplerate = mp3types_freqs[lsf + mpg25][samplerate];
  layer = 4 - ((header >> 17) & 0x3);
  bitrate = (header >> 12) & 0xF;
  bitrate = mp3types_bitrates[lsf][layer - 1][bitrate] * 1000;
  if (bitrate == 0)
    return 0;
  padding = (header >> 9) & 0x1;
  switch (layer) {
    case 1:
      length = (bitrate * 12) / samplerate + 4 * padding;
      break;
    case 2:
      length = (bitrate * 144) / samplerate + padding;
      break;
    default:
    case 3:
      length = (bitrate * 144) / (samplerate << lsf) + padding;
      break;
  }

  GST_DEBUG ("Calculated mp3 frame length of %u bytes", length);
  GST_DEBUG ("samplerate = %lu, bitrate = %lu, layer = %lu, channels = %lu",
	     samplerate, bitrate, layer, channels);

  if (put_layer)
    *put_layer = layer;
  if (put_channels)
    *put_channels = channels;
  if (put_bitrate)
    *put_bitrate = bitrate;
  if (put_samplerate)
    *put_samplerate = samplerate;

  return length;
}

/**
 * The chance that random data is identified as a valid mp3 header is 63 / 2^18
 * (0.024%) per try. This makes the function for calculating false positives
 *   1 - (1 - ((63 / 2 ^18) ^ GST_MP3_TYPEFIND_MIN_HEADERS)) ^ buffersize)
 * This has the following probabilities of false positives:
 * bufsize	          MIN_HEADERS
 * (bytes)	1	2	3	4
 * 4096		62.6%	 0.02%	 0%	 0%
 * 16384	98%	 0.09%	 0%	 0%
 * 1 MiB       100%	 5.88%	 0%	 0%
 * 1 GiB       100%    100%	 1.44%   0%
 * 1 TiB       100%    100%    100%      0.35%
 * This means that the current choice (3 headers by most of the time 4096 byte
 * buffers is pretty safe for now.
 *
 * The max. size of each frame is 1440 bytes, which means that for N frames
 * to be detected, we need 1440 * GST_MP3_TYPEFIND_MIN_HEADERS + 3 of data.
 * Assuming we step into the stream right after the frame header, this
 * means we need 1440 * (GST_MP3_TYPEFIND_MIN_HEADERS + 1) - 1 + 3 bytes
 * of data (5762) to always detect any mp3.
 */

/* increase this value when this function finds too many false positives */
#define GST_MP3_TYPEFIND_MIN_HEADERS 3
#define GST_MP3_TYPEFIND_MIN_DATA (1440 * (GST_MP3_TYPEFIND_MIN_HEADERS + 1) - 1 + 3)

static GstCaps *
mp3_caps_create (guint layer, guint channels,
		 guint bitrate, guint samplerate)
{
  GstCaps *new;

  g_assert (layer);
  g_assert (samplerate);
  g_assert (bitrate);
  g_assert (channels);

  new = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 1,
      "layer",       G_TYPE_INT, layer,
      "rate",        G_TYPE_INT, samplerate,
      "channels",    G_TYPE_INT, channels, NULL);

  return new;
}

static void
gst_mp3parse_base_init (GstMPEGAudioParseClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mp3_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mp3_src_template));
  gst_element_class_set_details (element_class, &mp3parse_details);
}

static void
gst_mp3parse_class_init (GstMPEGAudioParseClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SKIP,
    g_param_spec_int("skip","skip","skip",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BIT_RATE,
    g_param_spec_int("bitrate","Bitrate","Bit Rate",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mp3parse_set_property;
  gobject_class->get_property = gst_mp3parse_get_property;

  gstelement_class->change_state = gst_mp3parse_change_state;
}

static void
gst_mp3parse_init (GstMPEGAudioParse *mp3parse)
{
  mp3parse->sinkpad = gst_pad_new_from_template(
      gst_static_pad_template_get (&mp3_sink_template), "sink");
  gst_element_add_pad(GST_ELEMENT(mp3parse),mp3parse->sinkpad);

  gst_pad_set_chain_function(mp3parse->sinkpad,gst_mp3parse_chain);
  gst_element_set_loop_function (GST_ELEMENT(mp3parse),NULL);

  mp3parse->srcpad = gst_pad_new_from_template(
      gst_static_pad_template_get (&mp3_src_template), "src");
  gst_element_add_pad(GST_ELEMENT(mp3parse),mp3parse->srcpad);
  gst_pad_use_explicit_caps (mp3parse->srcpad);
  /*gst_pad_set_type_id(mp3parse->srcpad, mp3frametype); */

  mp3parse->partialbuf = NULL;
  mp3parse->skip = 0;
  mp3parse->in_flush = FALSE;

  mp3parse->rate = mp3parse->channels = mp3parse->layer = -1;
}

static void
gst_mp3parse_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstMPEGAudioParse *mp3parse;
  guchar *data;
  glong size,offset = 0;
  guint32 header;
  int bpf;
  GstBuffer *outbuf;
  guint64 last_ts;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
/*  g_return_if_fail(GST_IS_BUFFER(buf)); */

  mp3parse = GST_MP3PARSE (gst_pad_get_parent (pad));

  GST_DEBUG ("mp3parse: received buffer of %d bytes",GST_BUFFER_SIZE(buf));

  last_ts = GST_BUFFER_TIMESTAMP(buf);

  /* FIXME, do flush */
  /*
    if (mp3parse->partialbuf) {
      gst_buffer_unref(mp3parse->partialbuf);
      mp3parse->partialbuf = NULL;
    }
    mp3parse->in_flush = TRUE;
    */

  /* if we have something left from the previous frame */
  if (mp3parse->partialbuf) {

    mp3parse->partialbuf = gst_buffer_merge(mp3parse->partialbuf, buf);
    /* and the one we received.. */
    gst_buffer_unref(buf);
  }
  else {
    mp3parse->partialbuf = buf;
  }

  size = GST_BUFFER_SIZE(mp3parse->partialbuf);
  data = GST_BUFFER_DATA(mp3parse->partialbuf);

  /* while we still have bytes left -4 for the header */
  while (offset < size-4) {
    int skipped = 0;

    GST_DEBUG ("mp3parse: offset %ld, size %ld ",offset, size);

    /* search for a possible start byte */
    for (;((data[offset] != 0xff) && (offset < size));offset++) skipped++;
    if (skipped && !mp3parse->in_flush) {
      GST_DEBUG ("mp3parse: **** now at %ld skipped %d bytes",offset,skipped);
    }
    /* construct the header word */
    header = GUINT32_FROM_BE(*((guint32 *)(data+offset)));
    /* if it's a valid header, go ahead and send off the frame */
    if (head_check(header)) {
      /* calculate the bpf of the frame */
      bpf = bpf_from_header(mp3parse, header);

      /********************************************************************************
      * robust seek support
      * - This performs additional frame validation if the in_flush flag is set
      *   (indicating a discontinuous stream).
      * - The current frame header is not accepted as valid unless the NEXT frame
      *   header has the same values for most fields.  This significantly increases
      *   the probability that we aren't processing random data.
      * - It is not clear if this is sufficient for robust seeking of Layer III
      *   streams which utilize the concept of a "bit reservoir" by borrow bitrate
      *   from previous frames.  In this case, seeking may be more complicated because
      *   the frames are not independently coded.
      ********************************************************************************/
      if ( mp3parse->in_flush ) {
        guint32 header2;

        if ((size-offset)<(bpf+4)) { if (mp3parse->in_flush) break; } /* wait until we have the the entire current frame as well as the next frame header */

        header2 = GUINT32_FROM_BE(*((guint32 *)(data+offset+bpf)));
        GST_DEBUG ("mp3parse: header=%08X, header2=%08X, bpf=%d", (unsigned int)header, (unsigned int)header2, bpf );

/* mask the bits which are allowed to differ between frames */
#define HDRMASK ~((0xF << 12)  /* bitrate */ | \
		  (0x1 <<  9)  /* padding */ | \
		  (0x3 <<  4)) /*mode extension*/

        if ( (header2&HDRMASK) != (header&HDRMASK) ) { /* require 2 matching headers in a row */
           GST_DEBUG ("mp3parse: next header doesn't match (header=%08X, header2=%08X, bpf=%d)", (unsigned int)header, (unsigned int)header2, bpf );
           offset++; /* This frame is invalid.  Start looking for a valid frame at the next position in the stream */
           continue;
        }

      }

      /* if we don't have the whole frame... */
      if ((size - offset) < bpf) {
        GST_DEBUG ("mp3parse: partial buffer needed %ld < %d ",(size-offset), bpf);
	break;
      } else {
        guint bitrate, layer, rate, channels;

        if (!mp3_type_frame_length_from_header (header, &layer,
						&channels,
						&bitrate, &rate)) {
	  g_error("Header failed internal error");
        }
        if (channels != mp3parse->channels ||
            rate     != mp3parse->rate     ||
            layer    != mp3parse->layer    ||
            bitrate  != mp3parse->bit_rate) {
          GstCaps *caps = mp3_caps_create (layer, channels, bitrate, rate);

          gst_pad_set_explicit_caps(mp3parse->srcpad, caps);

          mp3parse->channels = channels;
          mp3parse->layer    = layer;
          mp3parse->rate     = rate;
          mp3parse->bit_rate = bitrate;
        }

        outbuf = gst_buffer_create_sub(mp3parse->partialbuf,offset,bpf);

        offset += bpf;
	if (mp3parse->skip == 0) {
          GST_DEBUG ("mp3parse: pushing buffer of %d bytes",GST_BUFFER_SIZE(outbuf));
	  if (mp3parse->in_flush) {
	    /* FIXME do some sort of flush event */
	    mp3parse->in_flush = FALSE;
	  }
	  GST_BUFFER_TIMESTAMP(outbuf) = last_ts;
	  GST_BUFFER_DURATION(outbuf) = 8 * (GST_SECOND/1000) * GST_BUFFER_SIZE(outbuf) / mp3parse->bit_rate;

          if (GST_PAD_CAPS (mp3parse->srcpad) != NULL) {
            gst_pad_push(mp3parse->srcpad,GST_DATA (outbuf));
          } else {
            GST_DEBUG ("No capsnego yet, delaying buffer push");
            gst_buffer_unref (outbuf);
          }
	}
	else {
          GST_DEBUG ("mp3parse: skipping buffer of %d bytes",GST_BUFFER_SIZE(outbuf));
          gst_buffer_unref(outbuf);
	  mp3parse->skip--;
	}
      }
    } else {
      offset++;
      if (!mp3parse->in_flush) GST_DEBUG ("mp3parse: *** wrong header, skipping byte (FIXME?)");
    }
  }
  /* if we have processed this block and there are still */
  /* bytes left not in a partial block, copy them over. */
  if (size-offset > 0) {
    glong remainder = (size - offset);
    GST_DEBUG ("mp3parse: partial buffer needed %ld for trailing bytes",remainder);

    outbuf = gst_buffer_create_sub(mp3parse->partialbuf,offset,remainder);
    gst_buffer_unref(mp3parse->partialbuf);
    mp3parse->partialbuf = outbuf;
  }
  else {
    gst_buffer_unref(mp3parse->partialbuf);
    mp3parse->partialbuf = NULL;
  }
}

static long
bpf_from_header (GstMPEGAudioParse *parse, unsigned long header)
{
  guint bitrate, layer, rate, channels, length;

  if (!(length = mp3_type_frame_length_from_header (header, &layer,
						    &channels,
						    &bitrate, &rate))) {
    return 0;
  }

  return length;
}

static gboolean
head_check (unsigned long head)
{
  GST_DEBUG ("checking mp3 header 0x%08lx",head);
  /* if it's not a valid sync */
  if ((head & 0xffe00000) != 0xffe00000) {
    GST_DEBUG ("invalid sync");return FALSE; }
  /* if it's an invalid MPEG version */
  if (((head >> 19) & 3) == 0x1) {
    GST_DEBUG ("invalid MPEG version");return FALSE; }
  /* if it's an invalid layer */
  if (!((head >> 17) & 3)) {
    GST_DEBUG ("invalid layer");return FALSE; }
  /* if it's an invalid bitrate */
  if (((head >> 12) & 0xf) == 0x0) {
    GST_DEBUG ("invalid bitrate");return FALSE; }
  if (((head >> 12) & 0xf) == 0xf) {
    GST_DEBUG ("invalid bitrate");return FALSE; }
  /* if it's an invalid samplerate */
  if (((head >> 10) & 0x3) == 0x3) {
    GST_DEBUG ("invalid samplerate");return FALSE; }
  if ((head & 0xffff0000) == 0xfffe0000) { 
    GST_DEBUG ("invalid sync");return FALSE; }
  if (head & 0x00000002) {
        GST_DEBUG ("invalid emphasis");return FALSE; }

  return TRUE;
}

static void
gst_mp3parse_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMPEGAudioParse *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MP3PARSE(object));
  src = GST_MP3PARSE(object);

  switch (prop_id) {
    case ARG_SKIP:
      src->skip = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_mp3parse_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMPEGAudioParse *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MP3PARSE(object));
  src = GST_MP3PARSE(object);

  switch (prop_id) {
    case ARG_SKIP:
      g_value_set_int (value, src->skip);
      break;
    case ARG_BIT_RATE:
      g_value_set_int (value, src->bit_rate * 1000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn 
gst_mp3parse_change_state (GstElement *element) 
{
  GstMPEGAudioParse *src;

  g_return_val_if_fail(GST_IS_MP3PARSE(element), GST_STATE_FAILURE);
  src = GST_MP3PARSE(element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      src->channels = -1; src->rate = -1; src->layer = -1;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "mp3parse",
			       GST_RANK_NONE, GST_TYPE_MP3PARSE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mpegaudioparse",
  "MPEG-1 layer 1/2/3 audio parser",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
