/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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
#  include "config.h"
#endif

#include "vorbisdec.h"
#include <string.h>
#include <gst/tag/tag.h>

static GstElementDetails vorbis_dec_details = {
  "VorbisDec",
  "Filter/Decoder/Audio",
  "decode raw vorbis streams to float audio",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
};

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

static GstStaticPadTemplate vorbis_dec_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-raw-float, "
      "rate = (int) [ 11025, 48000 ], "
      "channels = (int) [ 1, 2 ], "
      "endianness = (int) BYTE_ORDER, "
#ifdef GST_VORBIS_DEC_SEQUENTIAL
      "layout = \"sequential\", "
#endif
      "width = (int) 32, "
      "buffer-frames = (int) 0"
  )
);

static GstStaticPadTemplate vorbis_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-vorbis"
  )
);

GST_BOILERPLATE (GstVorbisDec, gst_vorbis_dec, GstElement, GST_TYPE_ELEMENT);
  
static void		vorbis_dec_chain		(GstPad *		pad,
							 GstData *		data);
static GstElementStateReturn 
			vorbis_dec_change_state		(GstElement *		element);
static gboolean		vorbis_dec_src_event		(GstPad *		pad,
							 GstEvent *		event);


static void
gst_vorbis_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_add_pad_template (element_class, 
      gst_static_pad_template_get (&vorbis_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vorbis_dec_sink_factory));
  gst_element_class_set_details (element_class, &vorbis_dec_details);
}

static void
gst_vorbis_dec_class_init (GstVorbisDecClass *klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = vorbis_dec_change_state;
}

static GstPadLinkReturn
vorbis_dec_link (GstPad *pad, const GstCaps *caps)
{
  GstVorbisDec *dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  if (dec->packetno < 3)
    return GST_PAD_LINK_DELAYED;

  return GST_PAD_LINK_OK;
}

static GstCaps *
vorbis_dec_getcaps (GstPad *pad)
{
  GstVorbisDec *dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  if (dec->packetno < 3)
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  return gst_caps_new_simple ("audio/x-raw-float",
      "rate",		G_TYPE_INT,	    dec->vi.rate,
      "channels",	G_TYPE_INT,	    dec->vi.channels,
      "endianness",	G_TYPE_INT,	    G_BYTE_ORDER,
      "width",		G_TYPE_INT,	    32,
#ifdef GST_VORBIS_DEC_SEQUENTIAL
      "layout",		G_TYPE_STRING,	    "sequential",
#endif
      "buffer-frames",	G_TYPE_INT,	    0,
      NULL);
}

static void
gst_vorbis_dec_init (GstVorbisDec *dec)
{
  dec->sinkpad = gst_pad_new_from_template(
      gst_static_pad_template_get (&vorbis_dec_sink_factory), "sink");
  gst_pad_set_chain_function (dec->sinkpad, vorbis_dec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_template(
      gst_static_pad_template_get (&vorbis_dec_src_factory), "src");
  gst_pad_set_link_function (dec->srcpad, vorbis_dec_link);
  gst_pad_set_getcaps_function (dec->srcpad, vorbis_dec_getcaps);
  gst_pad_set_event_function (dec->srcpad, vorbis_dec_src_event);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  GST_FLAG_SET (dec, GST_ELEMENT_EVENT_AWARE);
}

static gboolean
vorbis_dec_to_granulepos (GstVorbisDec *dec, GstFormat format, guint64 from, guint64 *to)
{
  if (dec->packetno < 1) return FALSE;
  
  switch (format) {
    case GST_FORMAT_TIME:
      *to = from * dec->vi.rate / GST_SECOND;
      return TRUE;
    case GST_FORMAT_DEFAULT:
      *to = from;
      return TRUE;
    case GST_FORMAT_BYTES:
      *to = from / sizeof (float) / dec->vi.channels;
      return TRUE;
    default:
      return FALSE;
  }
}

static gboolean
vorbis_dec_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstVorbisDec *dec;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK: {
      guint64 value;
      
      res = vorbis_dec_to_granulepos (dec, GST_EVENT_SEEK_FORMAT (event), 
	  GST_EVENT_SEEK_OFFSET (event), &value); 
      if (res) {
	GstEvent *real_seek = gst_event_new_seek (
	    (GST_EVENT_SEEK_TYPE (event) & ~GST_SEEK_FORMAT_MASK) | GST_FORMAT_DEFAULT,
	    value);
	res = gst_pad_send_event (GST_PAD_PEER (dec->sinkpad), real_seek);
      }
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_event_unref (event);
  return res;
}
static void
vorbis_dec_event (GstVorbisDec *dec, GstEvent *event)
{
  guint64 value;
  
  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT, &value)) {
	dec->granulepos = value;
	dec->packetno = 3;
	GST_DEBUG_OBJECT (dec, "setting granuleposition to %"G_GUINT64_FORMAT" after discont\n", value);
      } else {
	GST_WARNING_OBJECT (dec, 
	    "discont event didn't include offset, we might set it wrong now");
      }
      break;
    default:
      break;
  }
  gst_pad_event_default (dec->sinkpad, event);
}

static void
vorbis_dec_chain (GstPad *pad, GstData *data)
{
  GstBuffer *buf;
  GstVorbisDec *vd;
  ogg_packet packet; /* lol */

  vd = GST_VORBIS_DEC (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (data)) {
    vorbis_dec_event (vd, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buf);
  packet.bytes = GST_BUFFER_SIZE (buf);
  packet.granulepos = GST_BUFFER_OFFSET_END (buf);
  packet.packetno = vd->packetno ++;
  /* switch depending on packet type */
  if (packet.packet[0] & 1) {
    /* header packet */
    if (packet.packet[0] / 2 != packet.packetno) {
      /* FIXME: just skip? */
      gst_element_error (GST_ELEMENT (vd), STREAM, DECODE,
	  (NULL), ("unexpected packet type %d", (gint) packet.packet[0]));
      gst_data_unref (data);
      return;
    }
    if (vorbis_synthesis_headerin (&vd->vi, &vd->vc, &packet)) {
      gst_element_error (GST_ELEMENT (vd), STREAM, DECODE, 
	  (NULL), ("couldn't read header packet"));
      gst_data_unref (data);
      return;
    }
    if (packet.packetno == 1) {
      GstTagList *list = gst_tag_list_from_vorbiscomment_buffer (buf, "\003vorbis", 7, NULL);
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
	  GST_TAG_ENCODER_VERSION, vd->vi.version, NULL);
      if (vd->vi.bitrate_upper)
	gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, 
	    GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
      if (vd->vi.bitrate_nominal)
	gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, 
	    GST_TAG_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
      if (vd->vi.bitrate_lower)
	gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, 
	    GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);
      gst_element_found_tags_for_pad (GST_ELEMENT (vd), vd->srcpad, 0, list);
    } else if (packet.packetno == 2) {
      /* done */
      vorbis_synthesis_init (&vd->vd, &vd->vi);
      vorbis_block_init (&vd->vd, &vd->vb);
      if (gst_pad_is_linked (vd->srcpad))
	gst_pad_renegotiate (vd->srcpad);
    }
  } else {
    float **pcm;
    guint sample_count;
    
    /* normal data packet */
    if (vorbis_synthesis (&vd->vb, &packet)) {
      gst_element_error (GST_ELEMENT (vd), STREAM, DECODE, 
	  (NULL), ("couldn't read data packet"));
      gst_data_unref (data);
      return;
    }
    if (vorbis_synthesis_blockin (&vd->vd, &vd->vb) < 0) {
      gst_element_error (GST_ELEMENT (vd), STREAM, DECODE,
	  (NULL), ("vorbis decoder did not accept data packet"));
      gst_data_unref (data);
      return;
    }
    sample_count = vorbis_synthesis_pcmout (&vd->vd, &pcm);
    if (sample_count > 0) {
      int i, j;
      GstBuffer *out = gst_pad_alloc_buffer (vd->srcpad, GST_BUFFER_OFFSET_NONE, 
	  sample_count * vd->vi.channels * sizeof (float));
      float *out_data = (float *) GST_BUFFER_DATA (out);
#ifdef GST_VORBIS_DEC_SEQUENTIAL
      for (i = 0; i < vd->vi.channels; i++) {
	memcpy (out_data, pcm[i], sample_count * sizeof (float));
	out_data += sample_count;
      }
#else
      for (j = 0; j < sample_count; j++) {
	for (i = 0; i < vd->vi.channels; i++) {
	  *out_data = pcm[i][j];
	  out_data++;
	}
      }
#endif
      GST_BUFFER_OFFSET (out) = vd->granulepos;
      GST_BUFFER_OFFSET_END (out) = vd->granulepos + sample_count;
      GST_BUFFER_TIMESTAMP (out) = vd->granulepos * GST_SECOND / vd->vi.rate;
      GST_BUFFER_DURATION (out) = sample_count * GST_SECOND / vd->vi.rate;
      gst_pad_push (vd->srcpad, GST_DATA (out));
      vorbis_synthesis_read (&vd->vd, sample_count);
      vd->granulepos += sample_count;
    }
  }
  gst_data_unref (data);
}

static GstElementStateReturn
vorbis_dec_change_state (GstElement *element)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      vorbis_info_init (&vd->vi);
      vorbis_comment_init (&vd->vc);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      vorbis_block_clear (&vd->vb);
      vorbis_dsp_clear (&vd->vd);
      vorbis_comment_clear (&vd->vc);
      vorbis_info_clear (&vd->vi);
      vd->packetno = 0;
      vd->granulepos = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return parent_class->change_state (element);
}
