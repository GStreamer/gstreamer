/*  MP3 decoding plugin for GStreamer using the mpg123 library
 *  Copyright (C) 2012 Carlos Rafael Giani
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * SECTION: element-mpg123audiodec
 * @see_also: lamemp3enc, mad
 *
 * Audio decoder for MPEG-1 layer 1/2/3 audio data.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch filesrc location=music.mp3 ! mpegaudioparse ! mpg123audiodec ! audioconvert ! audioresample ! autoaudiosink
 * ]| Decode and play the mp3 file
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstmpg123audiodec.h"

#include <stdlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (mpg123_debug);
#define GST_CAT_DEFAULT mpg123_debug

/* Omitted sample formats that mpg123 supports (or at least can support):
 *  - 8bit integer signed
 *  - 8bit integer unsigned
 *  - a-law
 *  - mu-law
 *  - 64bit float
 *
 * The first four formats are not supported by the GstAudioDecoder base class.
 * (The internal gst_audio_format_from_caps_structure() call fails.)
 *
 * The 64bit float issue is tricky. mpg123 actually decodes to "real",
 * not necessarily to "float".
 *
 * "real" can be fixed point, 32bit float, 64bit float. There seems to be
 * no way how to find out which one of them is actually used.
 *
 * However, in all known installations, "real" equals 32bit float, so that's
 * what is used. */

static GstStaticPadTemplate static_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) { 1 }, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], " "parsed = (boolean) true ")
    );

static gboolean gst_mpg123_audio_dec_start (GstAudioDecoder * dec);
static gboolean gst_mpg123_audio_dec_stop (GstAudioDecoder * dec);
static GstFlowReturn gst_mpg123_audio_dec_push_decoded_bytes (GstMpg123AudioDec
    * mpg123_decoder, unsigned char const *decoded_bytes,
    size_t const num_decoded_bytes);
static GstFlowReturn gst_mpg123_audio_dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * input_buffer);
static gboolean gst_mpg123_audio_dec_set_format (GstAudioDecoder * dec,
    GstCaps * input_caps);
static void gst_mpg123_audio_dec_flush (GstAudioDecoder * dec, gboolean hard);

G_DEFINE_TYPE (GstMpg123AudioDec, gst_mpg123_audio_dec, GST_TYPE_AUDIO_DECODER);

static void
gst_mpg123_audio_dec_class_init (GstMpg123AudioDecClass * klass)
{
  GstAudioDecoderClass *base_class;
  GstElementClass *element_class;
  GstPadTemplate *src_template, *sink_template;
  int error;

  GST_DEBUG_CATEGORY_INIT (mpg123_debug, "mpg123", 0, "mpg123 mp3 decoder");

  base_class = GST_AUDIO_DECODER_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "mpg123 mp3 decoder",
      "Codec/Decoder/Audio",
      "Decodes mp3 streams using the mpg123 library",
      "Carlos Rafael Giani <dv@pseudoterminal.org>");

  /* Not using static pad template for srccaps, since the comma-separated list
   * of formats needs to be created depending on whatever mpg123 supports */
  {
    const int *format_list;
    const long *rates_list;
    size_t num, i;
    GString *s;
    GstCaps *src_template_caps;

    s = g_string_new ("audio/x-raw, ");

    mpg123_encodings (&format_list, &num);
    g_string_append (s, "format = { ");
    for (i = 0; i < num; ++i) {
      switch (format_list[i]) {
        case MPG123_ENC_SIGNED_16:
          g_string_append (s, (i > 0) ? ", " : "");
          g_string_append (s, GST_AUDIO_NE (S16));
          break;
        case MPG123_ENC_UNSIGNED_16:
          g_string_append (s, (i > 0) ? ", " : "");
          g_string_append (s, GST_AUDIO_NE (U16));
          break;
        case MPG123_ENC_SIGNED_24:
          g_string_append (s, (i > 0) ? ", " : "");
          g_string_append (s, GST_AUDIO_NE (S24));
          break;
        case MPG123_ENC_UNSIGNED_24:
          g_string_append (s, (i > 0) ? ", " : "");
          g_string_append (s, GST_AUDIO_NE (U24));
          break;
        case MPG123_ENC_SIGNED_32:
          g_string_append (s, (i > 0) ? ", " : "");
          g_string_append (s, GST_AUDIO_NE (S32));
          break;
        case MPG123_ENC_UNSIGNED_32:
          g_string_append (s, (i > 0) ? ", " : "");
          g_string_append (s, GST_AUDIO_NE (U32));
          break;
        case MPG123_ENC_FLOAT_32:
          g_string_append (s, (i > 0) ? ", " : "");
          g_string_append (s, GST_AUDIO_NE (F32));
          break;
        default:
          GST_DEBUG ("Ignoring mpg123 format %d", format_list[i]);
          break;
      }
    }
    g_string_append (s, " }, ");

    mpg123_rates (&rates_list, &num);
    g_string_append (s, "rate = (int) { ");
    for (i = 0; i < num; ++i) {
      g_string_append_printf (s, "%s%lu", (i > 0) ? ", " : "", rates_list[i]);
    }
    g_string_append (s, "}, ");

    g_string_append (s, "channels = (int) [ 1, 2 ], ");
    g_string_append (s, "layout = (string) interleaved");

    src_template_caps = gst_caps_from_string (s->str);
    src_template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
        src_template_caps);

    g_string_free (s, TRUE);
  }

  sink_template = gst_static_pad_template_get (&static_sink_template);

  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_add_pad_template (element_class, src_template);

  base_class->start = GST_DEBUG_FUNCPTR (gst_mpg123_audio_dec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_mpg123_audio_dec_stop);
  base_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_mpg123_audio_dec_handle_frame);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_mpg123_audio_dec_set_format);
  base_class->flush = GST_DEBUG_FUNCPTR (gst_mpg123_audio_dec_flush);

  error = mpg123_init ();
  if (G_UNLIKELY (error != MPG123_OK))
    GST_ERROR ("Could not initialize mpg123 library: %s",
        mpg123_plain_strerror (error));
  else
    GST_INFO ("mpg123 library initialized");
}


void
gst_mpg123_audio_dec_init (GstMpg123AudioDec * mpg123_decoder)
{
  mpg123_decoder->handle = NULL;
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (mpg123_decoder), TRUE);
}


static gboolean
gst_mpg123_audio_dec_start (GstAudioDecoder * dec)
{
  GstMpg123AudioDec *mpg123_decoder;
  int error;

  mpg123_decoder = GST_MPG123_AUDIO_DEC (dec);
  error = 0;

  mpg123_decoder->handle = mpg123_new (NULL, &error);
  mpg123_decoder->has_next_audioinfo = FALSE;
  mpg123_decoder->frame_offset = 0;

  /* Initially, the mpg123 handle comes with a set of default formats
   * supported. This clears this set.  This is necessary, since only one
   * format shall be supported (see set_format for more). */
  mpg123_format_none (mpg123_decoder->handle);

  /* Built-in mpg123 support for gapless decoding is disabled for now,
   * since it does not work well with seeking */
  mpg123_param (mpg123_decoder->handle, MPG123_REMOVE_FLAGS, MPG123_GAPLESS, 0);
  /* Tells mpg123 to use a small read-ahead buffer for better MPEG sync;
   * essential for MP3 radio streams */
  mpg123_param (mpg123_decoder->handle, MPG123_ADD_FLAGS, MPG123_SEEKBUFFER, 0);
  /* Sets the resync limit to the end of the stream (otherwise mpg123 may give
   * up on decoding prematurely, especially with mp3 web radios) */
  mpg123_param (mpg123_decoder->handle, MPG123_RESYNC_LIMIT, -1, 0);
#if MPG123_API_VERSION >= 36
  /* The precise API version where MPG123_AUTO_RESAMPLE appeared is
   * somewhere between 29 and 36 */
  /* Don't let mpg123 resample output */
  mpg123_param (mpg123_decoder->handle, MPG123_REMOVE_FLAGS,
      MPG123_AUTO_RESAMPLE, 0);
#endif
  /* Don't let mpg123 print messages to stdout/stderr */
  mpg123_param (mpg123_decoder->handle, MPG123_ADD_FLAGS, MPG123_QUIET, 0);

  /* Open in feed mode (= encoded data is fed manually into the handle). */
  error = mpg123_open_feed (mpg123_decoder->handle);

  if (G_UNLIKELY (error != MPG123_OK)) {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL),
        ("%s", mpg123_strerror (mpg123_decoder->handle)));
    mpg123_close (mpg123_decoder->handle);
    mpg123_delete (mpg123_decoder->handle);
    mpg123_decoder->handle = NULL;
    return FALSE;
  }

  GST_INFO_OBJECT (dec, "mpg123 decoder started");

  return TRUE;
}


static gboolean
gst_mpg123_audio_dec_stop (GstAudioDecoder * dec)
{
  GstMpg123AudioDec *mpg123_decoder = GST_MPG123_AUDIO_DEC (dec);

  if (G_LIKELY (mpg123_decoder->handle != NULL)) {
    mpg123_close (mpg123_decoder->handle);
    mpg123_delete (mpg123_decoder->handle);
    mpg123_decoder->handle = NULL;
  }

  GST_INFO_OBJECT (dec, "mpg123 decoder stopped");

  return TRUE;
}


static GstFlowReturn
gst_mpg123_audio_dec_push_decoded_bytes (GstMpg123AudioDec * mpg123_decoder,
    unsigned char const *decoded_bytes, size_t const num_decoded_bytes)
{
  GstBuffer *output_buffer;
  GstAudioDecoder *dec;

  output_buffer = NULL;
  dec = GST_AUDIO_DECODER (mpg123_decoder);

  if ((num_decoded_bytes == 0) || (decoded_bytes == NULL)) {
    /* This occurs in the first few frames, which do not carry data; once
     * MPG123_AUDIO_DEC_NEW_FORMAT is received, the empty frames stop occurring */
    GST_DEBUG_OBJECT (mpg123_decoder,
        "cannot decode yet, need more data -> no output buffer to push");
    return GST_FLOW_OK;
  }

  output_buffer = gst_buffer_new_allocate (NULL, num_decoded_bytes, NULL);

  if (output_buffer == NULL) {
    /* This is necessary to advance playback in time,
     * even when nothing was decoded. */
    return gst_audio_decoder_finish_frame (dec, NULL, 1);
  } else {
    GstMapInfo info;

    if (gst_buffer_map (output_buffer, &info, GST_MAP_WRITE)) {
      memcpy (info.data, decoded_bytes, num_decoded_bytes);
      gst_buffer_unmap (output_buffer, &info);
    } else {
      GST_ERROR_OBJECT (mpg123_decoder, "gst_buffer_map() returned NULL");
      gst_buffer_unref (output_buffer);
      output_buffer = NULL;
    }

    return gst_audio_decoder_finish_frame (dec, output_buffer, 1);
  }
}


static GstFlowReturn
gst_mpg123_audio_dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * input_buffer)
{
  GstMpg123AudioDec *mpg123_decoder;
  int decode_error;
  unsigned char *decoded_bytes;
  size_t num_decoded_bytes;
  GstFlowReturn retval;

  mpg123_decoder = GST_MPG123_AUDIO_DEC (dec);

  g_assert (mpg123_decoder->handle != NULL);

  /* The actual decoding */
  {
    /* feed input data (if there is any) */
    if (G_LIKELY (input_buffer != NULL)) {
      GstMapInfo info;

      if (gst_buffer_map (input_buffer, &info, GST_MAP_READ)) {
        mpg123_feed (mpg123_decoder->handle, info.data, info.size);
        gst_buffer_unmap (input_buffer, &info);
      } else {
        GST_AUDIO_DECODER_ERROR (mpg123_decoder, 1, RESOURCE, READ, (NULL),
            ("gst_memory_map() failed"), retval);
        return retval;
      }
    }

    /* Try to decode a frame */
    decoded_bytes = NULL;
    num_decoded_bytes = 0;
    decode_error = mpg123_decode_frame (mpg123_decoder->handle,
        &mpg123_decoder->frame_offset, &decoded_bytes, &num_decoded_bytes);
  }

  retval = GST_FLOW_OK;

  switch (decode_error) {
    case MPG123_NEW_FORMAT:
      /* As mentioned in gst_mpg123_audio_dec_set_format(), the next audioinfo
       * is not set immediately; instead, the code waits for mpg123 to take
       * note of the new format, and then sets the audioinfo. This fixes glitches
       * with mp3s containing several format headers (for example, first half
       * using 44.1kHz, second half 32 kHz) */

      GST_LOG_OBJECT (dec,
          "mpg123 reported a new format -> setting next srccaps");

      gst_mpg123_audio_dec_push_decoded_bytes (mpg123_decoder, decoded_bytes,
          num_decoded_bytes);

      /* If there is a next audioinfo, use it, then set has_next_audioinfo to
       * FALSE, to make sure gst_audio_decoder_set_output_format() isn't called
       * again until set_format is called by the base class */
      if (mpg123_decoder->has_next_audioinfo) {
        if (!gst_audio_decoder_set_output_format (dec,
                &(mpg123_decoder->next_audioinfo))) {
          GST_WARNING_OBJECT (dec, "Unable to set output format");
          retval = GST_FLOW_NOT_NEGOTIATED;
        }
        mpg123_decoder->has_next_audioinfo = FALSE;
      }

      break;

    case MPG123_NEED_MORE:
    case MPG123_OK:
      retval = gst_mpg123_audio_dec_push_decoded_bytes (mpg123_decoder,
          decoded_bytes, num_decoded_bytes);
      break;

    case MPG123_DONE:
      /* If this happens, then the upstream parser somehow missed the ending
       * of the bitstream */
      GST_LOG_OBJECT (dec, "mpg123 is done decoding");
      gst_mpg123_audio_dec_push_decoded_bytes (mpg123_decoder, decoded_bytes,
          num_decoded_bytes);
      retval = GST_FLOW_EOS;
      break;

    default:
    {
      /* Anything else is considered an error */
      int errcode;
      retval = GST_FLOW_ERROR;  /* use error by default */
      switch (decode_error) {
        case MPG123_ERR:
          errcode = mpg123_errcode (mpg123_decoder->handle);
          break;
        default:
          errcode = decode_error;
      }
      switch (errcode) {
        case MPG123_BAD_OUTFORMAT:{
          GstCaps *input_caps =
              gst_pad_get_current_caps (GST_AUDIO_DECODER_SINK_PAD (dec));
          GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
              ("Output sample format could not be used when trying to decode frame. "
                  "This is typically caused when the input caps (often the sample "
                  "rate) do not match the actual format of the audio data. "
                  "Input caps: %" GST_PTR_FORMAT, input_caps)
              );
          gst_caps_unref (input_caps);
          break;
        }
        default:{
          char const *errmsg = mpg123_plain_strerror (errcode);
          /* GST_AUDIO_DECODER_ERROR sets a new return value according to
           * its estimations */
          GST_AUDIO_DECODER_ERROR (mpg123_decoder, 1, STREAM, DECODE, (NULL),
              ("mpg123 decoding error: %s", errmsg), retval);
        }
      }
    }
  }

  return retval;
}


static gboolean
gst_mpg123_audio_dec_set_format (GstAudioDecoder * dec, GstCaps * input_caps)
{
/* Using the parsed information upstream, and the list of allowed caps
 * downstream, this code tries to find a suitable audio info. It is important
 * to keep in mind that the rate and number of channels should never deviate
 * from the one the bitstream has, otherwise mpg123 has to mix channels and/or
 * resample (and as its docs say, its internal resampler is very crude). The
 * sample format, however, can be chosen freely, because the MPEG specs do not
 * mandate any special format. Therefore, rate and number of channels are taken
 * from upstream (which parsed the MPEG frames, so the input_caps contain
 * exactly the rate and number of channels the bitstream actually has), while
 * the sample format is chosen by trying out all caps that are allowed by
 * downstream. This way, the output is adjusted to what the downstream prefers.
 *
 * Also, the new output audio info is not set immediately. Instead, it is
 * considered the "next audioinfo". The code waits for mpg123 to notice the new
 * format (= when mpg123_decode_frame() returns MPG123_AUDIO_DEC_NEW_FORMAT),
 * and then sets the next audioinfo. Otherwise, the next audioinfo is set too
 * soon, which may cause problems with mp3s containing several format headers.
 * One example would be an mp3 with the first 30 seconds using 44.1 kHz, then
 * the next 30 seconds using 32 kHz. Rare, but possible.
 *
 * STEPS:
 *
 * 1. get rate and channels from input_caps
 * 2. get allowed caps from src pad
 * 3. for each structure in allowed caps:
 * 3.1. take format
 * 3.2. if the combination of format with rate and channels is unsupported by
 *      mpg123, go to (3), or exit with error if there are no more structures
 *      to try
 * 3.3. create next audioinfo out of rate,channels,format, and exit
 */


  int rate, channels;
  GstMpg123AudioDec *mpg123_decoder;
  GstCaps *allowed_srccaps;
  guint structure_nr;
  gboolean match_found = FALSE;

  mpg123_decoder = GST_MPG123_AUDIO_DEC (dec);

  g_assert (mpg123_decoder->handle != NULL);

  mpg123_decoder->has_next_audioinfo = FALSE;

  /* Get rate and channels from input_caps */
  {
    GstStructure *structure;
    gboolean err = FALSE;

    /* Only the first structure is used (multiple
     * input caps structures don't make sense */
    structure = gst_caps_get_structure (input_caps, 0);

    if (!gst_structure_get_int (structure, "rate", &rate)) {
      err = TRUE;
      GST_ERROR_OBJECT (dec, "Input caps do not have a rate value");
    }
    if (!gst_structure_get_int (structure, "channels", &channels)) {
      err = TRUE;
      GST_ERROR_OBJECT (dec, "Input caps do not have a channel value");
    }

    if (err)
      return FALSE;
  }

  /* Get the caps that are allowed by downstream */
  {
    GstCaps *allowed_srccaps_unnorm =
        gst_pad_get_allowed_caps (GST_AUDIO_DECODER_SRC_PAD (dec));
    if (!allowed_srccaps_unnorm) {
      GST_ERROR_OBJECT (dec, "Allowed src caps are NULL");
      return FALSE;
    }
    allowed_srccaps = gst_caps_normalize (allowed_srccaps_unnorm);
  }

  /* Go through all allowed caps, pick the first one that matches */
  for (structure_nr = 0; structure_nr < gst_caps_get_size (allowed_srccaps);
      ++structure_nr) {
    GstStructure *structure;
    gchar const *format_str;
    GstAudioFormat format;
    int encoding;

    structure = gst_caps_get_structure (allowed_srccaps, structure_nr);

    format_str = gst_structure_get_string (structure, "format");
    if (format_str == NULL) {
      GST_DEBUG_OBJECT (dec, "Could not get format from src caps");
      continue;
    }

    format = gst_audio_format_from_string (format_str);
    if (format == GST_AUDIO_FORMAT_UNKNOWN) {
      GST_DEBUG_OBJECT (dec, "Unknown format %s", format_str);
      continue;
    }

    switch (format) {
      case GST_AUDIO_FORMAT_S16:
        encoding = MPG123_ENC_SIGNED_16;
        break;
      case GST_AUDIO_FORMAT_S24:
        encoding = MPG123_ENC_SIGNED_24;
        break;
      case GST_AUDIO_FORMAT_S32:
        encoding = MPG123_ENC_SIGNED_32;
        break;
      case GST_AUDIO_FORMAT_U16:
        encoding = MPG123_ENC_UNSIGNED_16;
        break;
      case GST_AUDIO_FORMAT_U24:
        encoding = MPG123_ENC_UNSIGNED_24;
        break;
      case GST_AUDIO_FORMAT_U32:
        encoding = MPG123_ENC_UNSIGNED_32;
        break;
      case GST_AUDIO_FORMAT_F32:
        encoding = MPG123_ENC_FLOAT_32;
        break;
      default:
        GST_DEBUG_OBJECT (dec,
            "Format %s in srccaps is not supported", format_str);
        continue;
    }

    {
      int err;

      /* Cleanup old formats & set new one */
      mpg123_format_none (mpg123_decoder->handle);
      err = mpg123_format (mpg123_decoder->handle, rate, channels, encoding);
      if (err != MPG123_OK) {
        GST_DEBUG_OBJECT (dec,
            "mpg123 cannot use caps %" GST_PTR_FORMAT
            " because mpg123_format() failed: %s", structure,
            mpg123_strerror (mpg123_decoder->handle));
        continue;
      }
    }

    gst_audio_info_init (&(mpg123_decoder->next_audioinfo));
    gst_audio_info_set_format (&(mpg123_decoder->next_audioinfo), format, rate,
        channels, NULL);
    GST_LOG_OBJECT (dec, "The next audio format is: %s, %u Hz, %u channels",
        format_str, rate, channels);
    mpg123_decoder->has_next_audioinfo = TRUE;

    match_found = TRUE;

    break;
  }

  gst_caps_unref (allowed_srccaps);

  return match_found;
}


static void
gst_mpg123_audio_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  int error;
  GstMpg123AudioDec *mpg123_decoder;

  GST_LOG_OBJECT (dec, "Flushing decoder");

  mpg123_decoder = GST_MPG123_AUDIO_DEC (dec);

  g_assert (mpg123_decoder->handle != NULL);

  /* Flush by reopening the feed */
  mpg123_close (mpg123_decoder->handle);
  error = mpg123_open_feed (mpg123_decoder->handle);

  if (G_UNLIKELY (error != MPG123_OK)) {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL),
        ("Error while reopening mpg123 feed: %s",
            mpg123_plain_strerror (error)));
    mpg123_close (mpg123_decoder->handle);
    mpg123_delete (mpg123_decoder->handle);
    mpg123_decoder->handle = NULL;
  }

  mpg123_decoder->has_next_audioinfo = FALSE;

  /* opening/closing feeds do not affect the format defined by the
   * mpg123_format() call that was made in gst_mpg123_audio_dec_set_format(),
   * and since the up/downstream caps are not expected to change here, no
   * mpg123_format() calls are done */
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mpg123audiodec",
      GST_RANK_MARGINAL, gst_mpg123_audio_dec_get_type ());
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpg123, "mp3 decoding based on the mpg123 library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
