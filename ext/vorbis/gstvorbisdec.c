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

/**
 * SECTION:element-vorbisdec
 * @see_also: vorbisenc, oggdemux
 *
 * This element decodes a Vorbis stream to raw float audio.
 * <ulink url="http://www.vorbis.com/">Vorbis</ulink> is a royalty-free
 * audio codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! alsasink
 * ]| Decode an Ogg/Vorbis. To create an Ogg/Vorbis file refer to the documentation of vorbisenc.
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvorbisdec.h"
#include <string.h>
#include <gst/audio/audio.h>
#include <gst/tag/tag.h>
#include <gst/audio/multichannel.h>

#include "gstvorbiscommon.h"

GST_DEBUG_CATEGORY_EXTERN (vorbisdec_debug);
#define GST_CAT_DEFAULT vorbisdec_debug

static GstStaticPadTemplate vorbis_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_VORBIS_DEC_SRC_CAPS);

static GstStaticPadTemplate vorbis_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstVorbisDec, gst_vorbis_dec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER);

static void vorbis_dec_finalize (GObject * object);

static gboolean vorbis_dec_start (GstAudioDecoder * dec);
static gboolean vorbis_dec_stop (GstAudioDecoder * dec);
static GstFlowReturn vorbis_dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);
static void vorbis_dec_flush (GstAudioDecoder * dec, gboolean hard);

static void
gst_vorbis_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &vorbis_dec_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &vorbis_dec_sink_factory);

  gst_element_class_set_details_simple (element_class,
      "Vorbis audio decoder", "Codec/Decoder/Audio",
      GST_VORBIS_DEC_DESCRIPTION,
      "Benjamin Otte <otte@gnome.org>, Chris Lord <chris@openedhand.com>");
}

static void
gst_vorbis_dec_class_init (GstVorbisDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS (klass);

  gobject_class->finalize = vorbis_dec_finalize;

  base_class->start = GST_DEBUG_FUNCPTR (vorbis_dec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (vorbis_dec_stop);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (vorbis_dec_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR (vorbis_dec_flush);
}

static void
gst_vorbis_dec_init (GstVorbisDec * dec, GstVorbisDecClass * g_class)
{
}

static void
vorbis_dec_finalize (GObject * object)
{
  /* Release any possibly allocated libvorbis data.
   * _clear functions can safely be called multiple times
   */
  GstVorbisDec *vd = GST_VORBIS_DEC (object);

#ifndef USE_TREMOLO
  vorbis_block_clear (&vd->vb);
#endif
  vorbis_dsp_clear (&vd->vd);
  vorbis_comment_clear (&vd->vc);
  vorbis_info_clear (&vd->vi);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vorbis_dec_reset (GstVorbisDec * dec)
{
  if (dec->taglist)
    gst_tag_list_free (dec->taglist);
  dec->taglist = NULL;
}

static gboolean
vorbis_dec_start (GstAudioDecoder * dec)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (dec);

  GST_DEBUG_OBJECT (dec, "start");
  vorbis_info_init (&vd->vi);
  vorbis_comment_init (&vd->vc);
  vd->initialized = FALSE;
  gst_vorbis_dec_reset (vd);

  return TRUE;
}

static gboolean
vorbis_dec_stop (GstAudioDecoder * dec)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");
  vd->initialized = FALSE;
#ifndef USE_TREMOLO
  vorbis_block_clear (&vd->vb);
#endif
  vorbis_dsp_clear (&vd->vd);
  vorbis_comment_clear (&vd->vc);
  vorbis_info_clear (&vd->vi);
  gst_vorbis_dec_reset (vd);

  return TRUE;
}

#if 0
static gboolean
vorbis_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstVorbisDec *dec;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format, tformat;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;
      guint32 seqnum;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);
      seqnum = gst_event_get_seqnum (event);
      gst_event_unref (event);

      /* First bring the requested format to time */
      tformat = GST_FORMAT_TIME;
      if (!(res = vorbis_dec_convert (pad, format, cur, &tformat, &tcur)))
        goto convert_error;
      if (!(res = vorbis_dec_convert (pad, format, stop, &tformat, &tstop)))
        goto convert_error;

      /* then seek with time on the peer */
      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);
      gst_event_set_seqnum (real_seek, seqnum);

      res = gst_pad_push_event (dec->sinkpad, real_seek);
      break;
    }
    default:
      res = gst_pad_push_event (dec->sinkpad, event);
      break;
  }
done:
  gst_object_unref (dec);

  return res;

  /* ERRORS */
convert_error:
  {
    GST_DEBUG_OBJECT (dec, "cannot convert start/stop for seek");
    goto done;
  }
}
#endif

static GstFlowReturn
vorbis_handle_identification_packet (GstVorbisDec * vd)
{
  GstCaps *caps;
  const GstAudioChannelPosition *pos = NULL;
  gint width = GST_VORBIS_DEC_DEFAULT_SAMPLE_WIDTH;

  switch (vd->vi.channels) {
    case 1:
    case 2:
      /* nothing */
      break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
      pos = gst_vorbis_channel_positions[vd->vi.channels - 1];
      break;
    default:{
      gint i;
      GstAudioChannelPosition *posn =
          g_new (GstAudioChannelPosition, vd->vi.channels);

      GST_ELEMENT_WARNING (GST_ELEMENT (vd), STREAM, DECODE,
          (NULL), ("Using NONE channel layout for more than 8 channels"));

      for (i = 0; i < vd->vi.channels; i++)
        posn[i] = GST_AUDIO_CHANNEL_POSITION_NONE;

      pos = posn;
    }
  }

  /* negotiate width with downstream */
  caps = gst_pad_get_allowed_caps (GST_AUDIO_DECODER_SRC_PAD (vd));
  if (caps) {
    if (!gst_caps_is_empty (caps)) {
      GstStructure *s;

      s = gst_caps_get_structure (caps, 0);
      /* template ensures 16 or 32 */
      gst_structure_get_int (s, "width", &width);

      GST_INFO_OBJECT (vd, "using %s with %d channels and %d bit audio depth",
          gst_structure_get_name (s), vd->vi.channels, width);
    }
    gst_caps_unref (caps);
  }
  vd->width = width >> 3;

  /* select a copy_samples function, this way we can have specialized versions
   * for mono/stereo and avoid the depth switch in tremor case */
  vd->copy_samples = get_copy_sample_func (vd->vi.channels, vd->width);

  caps =
      gst_caps_copy (gst_pad_get_pad_template_caps
      (GST_AUDIO_DECODER_SRC_PAD (vd)));
  gst_caps_set_simple (caps, "rate", G_TYPE_INT, vd->vi.rate, "channels",
      G_TYPE_INT, vd->vi.channels, "width", G_TYPE_INT, width, NULL);

  if (pos) {
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  }

  if (vd->vi.channels > 8) {
    g_free ((GstAudioChannelPosition *) pos);
  }

  gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (vd), caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_comment_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  guint bitrate = 0;
  gchar *encoder = NULL;
  GstTagList *list, *old_list;
  GstBuffer *buf;

  GST_DEBUG_OBJECT (vd, "parsing comment packet");

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = gst_ogg_packet_data (packet);
  GST_BUFFER_SIZE (buf) = gst_ogg_packet_size (packet);

  list =
      gst_tag_list_from_vorbiscomment_buffer (buf, (guint8 *) "\003vorbis", 7,
      &encoder);

  old_list = vd->taglist;
  vd->taglist = gst_tag_list_merge (vd->taglist, list, GST_TAG_MERGE_REPLACE);

  if (old_list)
    gst_tag_list_free (old_list);
  gst_tag_list_free (list);
  gst_buffer_unref (buf);

  if (!vd->taglist) {
    GST_ERROR_OBJECT (vd, "couldn't decode comments");
    vd->taglist = gst_tag_list_new ();
  }
  if (encoder) {
    if (encoder[0])
      gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
          GST_TAG_ENCODER, encoder, NULL);
    g_free (encoder);
  }
  gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_ENCODER_VERSION, vd->vi.version,
      GST_TAG_AUDIO_CODEC, "Vorbis", NULL);
  if (vd->vi.bitrate_nominal > 0 && vd->vi.bitrate_nominal <= 0x7FFFFFFF) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_NOMINAL_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
    bitrate = vd->vi.bitrate_nominal;
  }
  if (vd->vi.bitrate_upper > 0 && vd->vi.bitrate_upper <= 0x7FFFFFFF) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
    if (!bitrate)
      bitrate = vd->vi.bitrate_upper;
  }
  if (vd->vi.bitrate_lower > 0 && vd->vi.bitrate_lower <= 0x7FFFFFFF) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);
    if (!bitrate)
      bitrate = vd->vi.bitrate_lower;
  }
  if (bitrate) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, (guint) bitrate, NULL);
  }

  if (vd->initialized) {
    gst_element_found_tags_for_pad (GST_ELEMENT_CAST (vd),
        GST_AUDIO_DECODER_SRC_PAD (vd), vd->taglist);
    vd->taglist = NULL;
  } else {
    /* Only post them as messages for the time being. *
     * They will be pushed on the pad once the decoder is initialized */
    gst_element_post_message (GST_ELEMENT_CAST (vd),
        gst_message_new_tag (GST_OBJECT (vd), gst_tag_list_copy (vd->taglist)));
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_type_packet (GstVorbisDec * vd)
{
  gint res;

  g_assert (vd->initialized == FALSE);

#ifdef USE_TREMOLO
  if (G_UNLIKELY ((res = vorbis_dsp_init (&vd->vd, &vd->vi))))
    goto synthesis_init_error;
#else
  if (G_UNLIKELY ((res = vorbis_synthesis_init (&vd->vd, &vd->vi))))
    goto synthesis_init_error;

  if (G_UNLIKELY ((res = vorbis_block_init (&vd->vd, &vd->vb))))
    goto block_init_error;
#endif

  vd->initialized = TRUE;

  if (vd->taglist) {
    /* The tags have already been sent on the bus as messages. */
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (vd),
        gst_event_new_tag (vd->taglist));
    vd->taglist = NULL;
  }
  return GST_FLOW_OK;

  /* ERRORS */
synthesis_init_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't initialize synthesis (%d)", res));
    return GST_FLOW_ERROR;
  }
block_init_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't initialize block (%d)", res));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_handle_header_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  GstFlowReturn res;
  gint ret;

  GST_DEBUG_OBJECT (vd, "parsing header packet");

  /* Packetno = 0 if the first byte is exactly 0x01 */
  packet->b_o_s = ((gst_ogg_packet_data (packet))[0] == 0x1) ? 1 : 0;

#ifdef USE_TREMOLO
  if ((ret = vorbis_dsp_headerin (&vd->vi, &vd->vc, packet)))
#else
  if ((ret = vorbis_synthesis_headerin (&vd->vi, &vd->vc, packet)))
#endif
    goto header_read_error;

  switch ((gst_ogg_packet_data (packet))[0]) {
    case 0x01:
      res = vorbis_handle_identification_packet (vd);
      break;
    case 0x03:
      res = vorbis_handle_comment_packet (vd, packet);
      break;
    case 0x05:
      res = vorbis_handle_type_packet (vd);
      break;
    default:
      /* ignore */
      g_warning ("unknown vorbis header packet found");
      res = GST_FLOW_OK;
      break;
  }

  return res;

  /* ERRORS */
header_read_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't read header packet (%d)", ret));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_dec_handle_header_buffer (GstVorbisDec * vd, GstBuffer * buffer)
{
  ogg_packet *packet;
  ogg_packet_wrapper packet_wrapper;

  gst_ogg_packet_wrapper_from_buffer (&packet_wrapper, buffer);
  packet = gst_ogg_packet_from_wrapper (&packet_wrapper);

  return vorbis_handle_header_packet (vd, packet);
}

#define MIN_NUM_HEADERS 3
static GstFlowReturn
vorbis_dec_handle_header_caps (GstVorbisDec * vd)
{
  GstFlowReturn result = GST_FLOW_OK;
  GstCaps *caps;
  GstStructure *s = NULL;
  const GValue *array = NULL;

  caps = GST_PAD_CAPS (GST_AUDIO_DECODER_SINK_PAD (vd));
  if (caps)
    s = gst_caps_get_structure (caps, 0);
  if (s)
    array = gst_structure_get_value (s, "streamheader");

  if (array && (gst_value_array_get_size (array) >= MIN_NUM_HEADERS)) {
    const GValue *value = NULL;
    GstBuffer *buf = NULL;
    gint i = 0;

    while (result == GST_FLOW_OK && i < gst_value_array_get_size (array)) {
      value = gst_value_array_get_value (array, i);
      buf = gst_value_get_buffer (value);
      if (!buf)
        goto null_buffer;
      result = vorbis_dec_handle_header_buffer (vd, buf);
      i++;
    }
  } else
    goto array_error;

done:
  return (result != GST_FLOW_OK ? GST_FLOW_NOT_NEGOTIATED : GST_FLOW_OK);

  /* ERRORS */
array_error:
  {
    GST_WARNING_OBJECT (vd, "streamheader array not found");
    result = GST_FLOW_ERROR;
    goto done;
  }
null_buffer:
  {
    GST_WARNING_OBJECT (vd, "streamheader with null buffer received");
    result = GST_FLOW_ERROR;
    goto done;
  }
}


static GstFlowReturn
vorbis_handle_data_packet (GstVorbisDec * vd, ogg_packet * packet,
    GstClockTime timestamp, GstClockTime duration)
{
#ifdef USE_TREMOLO
  vorbis_sample_t *pcm;
#else
  vorbis_sample_t **pcm;
#endif
  guint sample_count;
  GstBuffer *out = NULL;
  GstFlowReturn result;
  gint size;

  if (G_UNLIKELY (!vd->initialized)) {
    result = vorbis_dec_handle_header_caps (vd);
    if (result != GST_FLOW_OK)
      goto not_initialized;
  }

  /* normal data packet */
  /* FIXME, we can skip decoding if the packet is outside of the
   * segment, this is however not very trivial as we need a previous
   * packet to decode the current one so we must be careful not to
   * throw away too much. For now we decode everything and clip right
   * before pushing data. */

#ifdef USE_TREMOLO
  if (G_UNLIKELY (vorbis_dsp_synthesis (&vd->vd, packet, 1)))
    goto could_not_read;
#else
  if (G_UNLIKELY (vorbis_synthesis (&vd->vb, packet)))
    goto could_not_read;

  if (G_UNLIKELY (vorbis_synthesis_blockin (&vd->vd, &vd->vb) < 0))
    goto not_accepted;
#endif

  /* assume all goes well here */
  result = GST_FLOW_OK;

  /* count samples ready for reading */
#ifdef USE_TREMOLO
  if ((sample_count = vorbis_dsp_pcmout (&vd->vd, NULL, 0)) == 0)
#else
  if ((sample_count = vorbis_synthesis_pcmout (&vd->vd, NULL)) == 0)
    goto done;
#endif

  size = sample_count * vd->vi.channels * vd->width;
  GST_LOG_OBJECT (vd, "%d samples ready for reading, size %d", sample_count,
      size);

  /* alloc buffer for it */
  result =
      gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_DECODER_SRC_PAD (vd),
      GST_BUFFER_OFFSET_NONE, size,
      GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (vd)), &out);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto done;

  /* get samples ready for reading now, should be sample_count */
#ifdef USE_TREMOLO
  pcm = GST_BUFFER_DATA (out);
  if (G_UNLIKELY (vorbis_dsp_pcmout (&vd->vd, pcm, sample_count) !=
          sample_count))
#else
  if (G_UNLIKELY (vorbis_synthesis_pcmout (&vd->vd, &pcm) != sample_count))
#endif
    goto wrong_samples;

#ifndef USE_TREMOLO
  /* copy samples in buffer */
  vd->copy_samples ((vorbis_sample_t *) GST_BUFFER_DATA (out), pcm,
      sample_count, vd->vi.channels, vd->width);
#endif

  GST_LOG_OBJECT (vd, "setting output size to %d", size);
  GST_BUFFER_SIZE (out) = size;

done:
  /* whether or not data produced, consume one frame and advance time */
  result = gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (vd), out, 1);

#ifdef USE_TREMOLO
  vorbis_dsp_read (&vd->vd, sample_count);
#else
  vorbis_synthesis_read (&vd->vd, sample_count);
#endif

  return result;

  /* ERRORS */
not_initialized:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("no header sent yet"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
could_not_read:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't read data packet"));
    return GST_FLOW_ERROR;
  }
not_accepted:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("vorbis decoder did not accept data packet"));
    return GST_FLOW_ERROR;
  }
wrong_samples:
  {
    gst_buffer_unref (out);
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("vorbis decoder reported wrong number of samples"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * buffer)
{
  ogg_packet *packet;
  ogg_packet_wrapper packet_wrapper;
  GstFlowReturn result = GST_FLOW_OK;
  GstVorbisDec *vd = GST_VORBIS_DEC (dec);

  /* no draining etc */
  if (G_UNLIKELY (!buffer))
    return GST_FLOW_OK;

  /* make ogg_packet out of the buffer */
  gst_ogg_packet_wrapper_from_buffer (&packet_wrapper, buffer);
  packet = gst_ogg_packet_from_wrapper (&packet_wrapper);
  /* set some more stuff */
  packet->granulepos = -1;
  packet->packetno = 0;         /* we don't care */
  /* EOS does not matter, it is used in vorbis to implement clipping the last
   * block of samples based on the granulepos. We clip based on segments. */
  packet->e_o_s = 0;

  GST_LOG_OBJECT (vd, "decode buffer of size %ld", packet->bytes);

  /* error out on empty header packets, but just skip empty data packets */
  if (G_UNLIKELY (packet->bytes == 0)) {
    if (vd->initialized)
      goto empty_buffer;
    else
      goto empty_header;
  }

  /* switch depending on packet type */
  if ((gst_ogg_packet_data (packet))[0] & 1) {
    if (vd->initialized) {
      GST_WARNING_OBJECT (vd, "Already initialized, so ignoring header packet");
      goto done;
    }
    result = vorbis_handle_header_packet (vd, packet);
    /* consumer header packet/frame */
    gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (vd), NULL, 1);
  } else {
    GstClockTime timestamp, duration;

    timestamp = GST_BUFFER_TIMESTAMP (buffer);
    duration = GST_BUFFER_DURATION (buffer);

    result = vorbis_handle_data_packet (vd, packet, timestamp, duration);
  }

done:
  return result;

empty_buffer:
  {
    /* don't error out here, just ignore the buffer, it's invalid for vorbis
     * but not fatal. */
    GST_WARNING_OBJECT (vd, "empty buffer received, ignoring");
    result = GST_FLOW_OK;
    goto done;
  }

/* ERRORS */
empty_header:
  {
    GST_ELEMENT_ERROR (vd, STREAM, DECODE, (NULL), ("empty header received"));
    result = GST_FLOW_ERROR;
    goto done;
  }
}

static void
vorbis_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (dec);

#ifdef HAVE_VORBIS_SYNTHESIS_RESTART
  vorbis_synthesis_restart (&vd->vd);
#endif

  if (hard)
    gst_vorbis_dec_reset (vd);
}
