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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/getbits/getbits.h>

#include "buffer.h"

#define SEQUENCE_HEADER         0x000001b3
#define SEQUENCE_END            0x000001b7
#define PICTURE_START           0x00000100
#define GROUP_START             0x000001b8
#define SYNCWORD_START          0x000001

#define AUDIO_SYNCWORD          0xfff

#define CLOCKS                  90000.0

#ifdef G_HAVE_ISO_VARARGS

#define DEBUG(...) g_print (__VA_ARGS__)

#elif defined(G_HAVE_GNUC_VARARGS)

#define DEBUG(a, b...) g_print (##b)

#endif

/* This must match decoder and encoder tables */
static double picture_rates[16] = {
  0.0,
  24000.0 / 1001.,
  24.0,
  25.0,
  30000.0 / 1001.,
  30.0,
  50.0,
  60000.0 / 1001.,
  60.0,

  1,
  5,
  10,
  12,
  15,
  0,
  0
};

/* defined but not used
static double ratio [16] = { 0., 1., 0.6735, 0.7031, 0.7615, 0.8055,
	0.8437, 0.8935, 0.9157, 0.9815, 1.0255, 1.0695, 1.0950, 1.1575,
	1.2015, 0.};
*/

#ifndef GST_DISABLE_GST_DEBUG
static char picture_types[4][3] = { "I", "P", "B", "D" };
#endif

static int bitrate_index[2][3][16] =
    { {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,},
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,},
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,}},
{{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,}},
};

static long frequency[9] =
    { 44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000 };

static double dfrequency[9] = { 44.1, 48, 32, 22.05, 24, 16, 11.025, 12, 8 };

static unsigned int samples[4] = { 192, 384, 1152, 1152 };

/* deined but not used
static char mode [4][15] =
    { "stereo", "joint stereo", "dual channel", "single channel" };
static char copyright [2][20] =
    { "no copyright","copyright protected" };
static char original [2][10] =
    { "copy","original" };
static char emphasis [4][20] =
    { "none", "50/15 microseconds", "reserved", "CCITT J.17" };
*/
static void mpeg1mux_buffer_update_video_info (Mpeg1MuxBuffer * mb);
static void mpeg1mux_buffer_update_audio_info (Mpeg1MuxBuffer * mb);

Mpeg1MuxBuffer *
mpeg1mux_buffer_new (guchar type, guchar id)
{
  Mpeg1MuxBuffer *new = g_malloc (sizeof (Mpeg1MuxBuffer));

  new->buffer = NULL;
  new->length = 0;
  new->base = 0;
  new->buffer_type = type;
  new->stream_id = id;
  new->scan_pos = 0;
  new->new_frame = TRUE;
  new->current_start = 0;
  new->timecode_list = NULL;
  new->queued_list = NULL;
  new->next_frame_time = 0;

  return new;
}

void
mpeg1mux_buffer_queue (Mpeg1MuxBuffer * mb, GstBuffer * buf)
{

  if (mb->buffer == NULL) {
    mb->buffer = g_malloc (GST_BUFFER_SIZE (buf));
    mb->length = GST_BUFFER_SIZE (buf);
    memcpy (mb->buffer, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  } else {
    mb->buffer = g_realloc (mb->buffer, mb->length + GST_BUFFER_SIZE (buf));
    memcpy (mb->buffer + mb->length, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    mb->length += GST_BUFFER_SIZE (buf);
  }

  GST_DEBUG ("queuing buffer %lu", mb->length);
  if (mb->buffer_type == BUFFER_TYPE_VIDEO) {
    mpeg1mux_buffer_update_video_info (mb);
  } else {
    mpeg1mux_buffer_update_audio_info (mb);
  }
}

gulong
mpeg1mux_buffer_update_queued (Mpeg1MuxBuffer * mb, guint64 scr)
{
  GList *queued_list;
  Mpeg1MuxTimecode *tc;
  gulong total_queued = 0;

  GST_DEBUG ("queued in buffer on SCR=%" G_GUINT64_FORMAT, scr);
  queued_list = g_list_first (mb->queued_list);

  while (queued_list) {
    tc = (Mpeg1MuxTimecode *) queued_list->data;
    if (tc->DTS < scr) {
      /* this buffer should be sent out  */
      mb->queued_list = g_list_remove (mb->queued_list, tc);
      queued_list = g_list_first (mb->queued_list);
    } else {
      GST_DEBUG ("queued in buffer %ld, %" G_GUINT64_FORMAT,
          tc->original_length, tc->DTS);
      total_queued += tc->original_length;
      queued_list = g_list_next (queued_list);
    }
  }
  GST_DEBUG ("queued in buffer %lu", total_queued);

  return total_queued;
}

void
mpeg1mux_buffer_shrink (Mpeg1MuxBuffer * mb, gulong size)
{
  GList *timecode_list;
  Mpeg1MuxTimecode *tc;
  gulong consumed = 0;
  gulong count;

  GST_DEBUG ("shrinking buffer %lu", size);

  g_assert (mb->length >= size);

  memcpy (mb->buffer, mb->buffer + size, mb->length - size);
  mb->buffer = g_realloc (mb->buffer, mb->length - size);

  mb->length -= size;
  mb->scan_pos -= size;
  mb->current_start -= size;

  timecode_list = g_list_first (mb->timecode_list);
  tc = (Mpeg1MuxTimecode *) timecode_list->data;

  if (tc->length > size) {
    tc->length -= size;
    mb->new_frame = FALSE;
  } else {
    consumed += tc->length;
    while (size >= consumed) {
      GST_DEBUG ("removing timecode: %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT
          " %lu %lu", tc->DTS, tc->PTS, tc->length, consumed);
      mb->timecode_list = g_list_remove_link (mb->timecode_list, timecode_list);
      mb->queued_list = g_list_append (mb->queued_list, tc);
      timecode_list = g_list_first (mb->timecode_list);
      tc = (Mpeg1MuxTimecode *) timecode_list->data;
      consumed += tc->length;
      GST_DEBUG ("next timecode: %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT
          " %lu %lu", tc->DTS, tc->PTS, tc->length, consumed);
    }
    mb->new_frame = TRUE;
    GST_DEBUG ("leftover frame size from %lu to %lu ", tc->length,
        consumed - size);
    tc->length = consumed - size;
  }

  if (mb->buffer_type == BUFFER_TYPE_VIDEO) {
    mb->info.video.DTS = tc->DTS;
    mb->info.video.PTS = tc->PTS;
    mb->next_frame_time = tc->DTS;
  } else {
    mb->info.audio.PTS = tc->PTS;
    mb->next_frame_time = tc->PTS;
  }
  GST_DEBUG ("next frame time timecode: %" G_GUINT64_FORMAT " %lu",
      mb->next_frame_time, tc->length);

  /* check buffer consistency */
  timecode_list = g_list_first (mb->timecode_list);
  count = 0;

  while (timecode_list) {
    tc = (Mpeg1MuxTimecode *) timecode_list->data;
    count += tc->length;

    timecode_list = g_list_next (timecode_list);
  }

  if (count != mb->current_start)
    g_print ("********** error %lu != %lu\n", count, mb->current_start);

  mb->base += size;
}

static void
mpeg1mux_buffer_update_video_info (Mpeg1MuxBuffer * mb)
{
  gboolean have_sync = FALSE;
  guchar *data = mb->buffer;
  gulong offset = mb->scan_pos;
  guint sync_zeros = 0;
  gulong id = 0;
  guint temporal_reference, temp;
  gst_getbits_t gb;


  GST_DEBUG ("mpeg1mux::update_video_info %lu %lu", mb->base, mb->scan_pos);
  if (mb->base == 0 && mb->scan_pos == 0) {
    if ((SYNCWORD_START << 8) + *(mb->buffer + 3) == SEQUENCE_HEADER) {

      gst_getbits_init (&gb, NULL, NULL);
      gst_getbits_newbuf (&gb, data + 4, mb->length);
      mb->info.video.horizontal_size = gst_getbits12 (&gb);
      mb->info.video.vertical_size = gst_getbits12 (&gb);
      mb->info.video.aspect_ratio = gst_getbits4 (&gb);
      mb->info.video.picture_rate = gst_getbits4 (&gb);
      mb->info.video.bit_rate = gst_getbits18 (&gb);
      if (gst_getbits1 (&gb) != 1) {
        g_print ("mpeg1mux::update_video_info: marker bit error\n");
      }
      mb->info.video.vbv_buffer_size = gst_getbits10 (&gb);
      mb->info.video.CSPF = gst_getbits1 (&gb);

      mb->info.video.secs_per_frame =
          1. / picture_rates[mb->info.video.picture_rate];
      mb->info.video.decoding_order = 0;
      mb->info.video.group_order = 0;
      GST_DEBUG ("mpeg1mux::update_video_info: secs per frame %g",
          mb->info.video.secs_per_frame);
    } else {
      g_print ("mpeg1mux::update_video_info: Invalid MPEG Video header\n");
    }
  }
  while (offset < mb->length - 6) {
    if (!have_sync) {
      guchar byte = *(data + offset);

      /*GST_DEBUG ("mpeg1mux::update_video_info: found #%d at %lu",byte,offset); */
      offset++;
      /* if it's zero, increment the zero count */
      if (byte == 0) {
        sync_zeros++;
        /*GST_DEBUG ("mpeg1mux::update_video_info: found zero #%d at %lu",sync_zeros,offset-1); */
      }
      /* if it's a one and we have two previous zeros, we have sync */
      else if ((byte == 1) && (sync_zeros >= 2)) {
        GST_DEBUG ("mpeg1mux::update_video_info: synced at %lu", offset - 1);
        have_sync = TRUE;
        sync_zeros = 0;
      }
      /* if it's anything else, we've lost it completely */
      else
        sync_zeros = 0;
      /* then snag the chunk ID */
    } else if (id == 0) {
      id = *(data + offset);
      GST_DEBUG ("mpeg1mux::update_video_info: got id 0x%02lX", id);
      id = (SYNCWORD_START << 8) + id;
      switch (id) {
        case SEQUENCE_HEADER:
          GST_DEBUG ("mpeg1mux::update_video_info: sequence header");
          break;
        case GROUP_START:
          GST_DEBUG ("mpeg1mux::update_video_info: group start");
          mb->info.video.group_order = 0;
          break;
        case PICTURE_START:
          /* skip the first access unit */
          if (mb->info.video.decoding_order != 0) {
            Mpeg1MuxTimecode *tc;

            GST_DEBUG ("mpeg1mux::update_video_info: PTS %" G_GUINT64_FORMAT
                ", DTS %" G_GUINT64_FORMAT ", length %lu",
                mb->info.video.current_PTS, mb->info.video.current_DTS,
                offset - mb->current_start - 3);

            tc = (Mpeg1MuxTimecode *) g_malloc (sizeof (Mpeg1MuxTimecode));
            tc->length = offset - mb->current_start - 3;
            tc->original_length = tc->length;
            tc->frame_type = mb->info.video.current_type;
            tc->DTS = mb->info.video.current_DTS;
            tc->PTS = mb->info.video.current_PTS;

            mb->timecode_list = g_list_append (mb->timecode_list, tc);

            if (mb->info.video.decoding_order == 0) {
              mb->next_frame_time = tc->DTS;
            }

            mb->current_start = offset - 3;
          }

          temp = (*(data + offset + 1) << 8) + *(data + offset + 2);
          temporal_reference = (temp & 0xffc0) >> 6;
          mb->info.video.current_type = (temp & 0x0038) >> 3;
          GST_DEBUG
              ("mpeg1mux::update_video_info: picture start temporal_ref:%d type:%s Frame",
              temporal_reference,
              picture_types[mb->info.video.current_type - 1]);

          mb->info.video.current_DTS =
              mb->info.video.decoding_order * mb->info.video.secs_per_frame *
              CLOCKS;
          mb->info.video.current_PTS =
              (temporal_reference - mb->info.video.group_order + 1 +
              mb->info.video.decoding_order) * mb->info.video.secs_per_frame *
              CLOCKS;

          mb->info.video.decoding_order++;
          mb->info.video.group_order++;


          offset++;
          break;
        case SEQUENCE_END:
          GST_DEBUG ("mpeg1mux::update_video_info: sequence end");
          break;
      }
      /* prepare for next sync */
      offset++;
      have_sync = FALSE;
      id = 0;
      sync_zeros = 0;
    }
  }
  mb->scan_pos = offset;
}

static void
mpeg1mux_buffer_update_audio_info (Mpeg1MuxBuffer * mb)
{
  guchar *data = mb->buffer;
  gulong offset = mb->scan_pos;
  guint32 id = 0;
  guint padding_bit;
  gst_getbits_t gb;
  guint startup_delay = 0;
  int layer_index, lsf, samplerate_index, padding;
  long bpf;
  Mpeg1MuxTimecode *tc;


  GST_DEBUG ("mpeg1mux::update_audio_info %lu %lu", mb->base, mb->scan_pos);
  if (mb->base == 0 && mb->scan_pos == 0) {
    id = GST_READ_UINT32_BE (data);

    printf ("MPEG audio id = %08x\n", (unsigned int) id);
    if ((id & 0xfff00000) == AUDIO_SYNCWORD << 20) {

      /* mpegver = (header >> 19) & 0x3; don't need this for bpf */
      layer_index = (id >> 17) & 0x3;
      mb->info.audio.layer = 4 - layer_index;
      lsf = (id & (1 << 20)) ? ((id & (1 << 19)) ? 0 : 1) : 1;
      mb->info.audio.bit_rate =
          bitrate_index[lsf][mb->info.audio.layer - 1][((id >> 12) & 0xf)];
      samplerate_index = (id >> 10) & 0x3;
      padding = (id >> 9) & 0x1;

      if (mb->info.audio.layer == 1) {
        bpf = mb->info.audio.bit_rate * 12000;
        bpf /= frequency[samplerate_index];
        bpf = ((bpf + padding) << 2);
      } else {
        bpf = mb->info.audio.bit_rate * 144000;
        bpf /= frequency[samplerate_index];
        bpf += padding;
      }
      mb->info.audio.framesize = bpf;

      GST_DEBUG ("mpeg1mux::update_audio_info: samples per second %d",
          samplerate_index);

      gst_getbits_init (&gb, NULL, NULL);
      gst_getbits_newbuf (&gb, data, mb->length);

      gst_flushbitsn (&gb, 12);
      if (gst_getbits1 (&gb) != 1) {
        g_print ("mpeg1mux::update_audio_info: marker bit error\n");
      }
      gst_flushbitsn (&gb, 2);
      mb->info.audio.protection = gst_getbits1 (&gb);
      gst_flushbitsn (&gb, 4);
      mb->info.audio.frequency = gst_getbits2 (&gb);
      padding_bit = gst_getbits1 (&gb);
      gst_flushbitsn (&gb, 1);
      mb->info.audio.mode = gst_getbits2 (&gb);
      mb->info.audio.mode_extension = gst_getbits2 (&gb);
      mb->info.audio.copyright = gst_getbits1 (&gb);
      mb->info.audio.original_copy = gst_getbits1 (&gb);
      mb->info.audio.emphasis = gst_getbits2 (&gb);

      GST_DEBUG ("mpeg1mux::update_audio_info: layer %d", mb->info.audio.layer);
      GST_DEBUG ("mpeg1mux::update_audio_info: bit_rate %d",
          mb->info.audio.bit_rate);
      GST_DEBUG ("mpeg1mux::update_audio_info: frequency %d",
          mb->info.audio.frequency);

      mb->info.audio.samples_per_second =
          (double) dfrequency[mb->info.audio.frequency];

      GST_DEBUG ("mpeg1mux::update_audio_info: samples per second %g",
          mb->info.audio.samples_per_second);

      mb->info.audio.decoding_order = 0;

      tc = (Mpeg1MuxTimecode *) g_malloc (sizeof (Mpeg1MuxTimecode));
      tc->length = mb->info.audio.framesize;
      tc->original_length = tc->length;
      tc->frame_type = FRAME_TYPE_AUDIO;

      mb->info.audio.current_PTS =
          mb->info.audio.decoding_order * samples[mb->info.audio.layer] /
          mb->info.audio.samples_per_second * 90. + startup_delay;

      GST_DEBUG ("mpeg1mux::update_audio_info: PTS %" G_GUINT64_FORMAT
          ", length %u", mb->info.audio.current_PTS, mb->info.audio.framesize);
      tc->PTS = mb->info.audio.current_PTS;
      tc->DTS = mb->info.audio.current_PTS;
      mb->timecode_list = g_list_append (mb->timecode_list, tc);

      mb->next_frame_time = tc->PTS;

      mb->info.audio.decoding_order++;
      offset += tc->length;
    } else {
      g_print ("mpeg1mux::update_audio_info: Invalid MPEG Video header\n");
    }
  }
  while (offset < mb->length - 4) {
    id = GST_READ_UINT32_BE (data + offset);

    /* mpegver = (header >> 19) & 0x3;  don't need this for bpf */
    layer_index = (id >> 17) & 0x3;
    mb->info.audio.layer = 4 - layer_index;
    lsf = (id & (1 << 20)) ? ((id & (1 << 19)) ? 0 : 1) : 1;
    mb->info.audio.bit_rate =
        bitrate_index[lsf][mb->info.audio.layer - 1][((id >> 12) & 0xf)];
    samplerate_index = (id >> 10) & 0x3;
    padding = (id >> 9) & 0x1;

    if (mb->info.audio.layer == 1) {
      bpf = mb->info.audio.bit_rate * 12000;
      bpf /= frequency[samplerate_index];
      bpf = ((bpf + padding) << 2);
    } else {
      bpf = mb->info.audio.bit_rate * 144000;
      bpf /= frequency[samplerate_index];
      bpf += padding;
    }
    tc = (Mpeg1MuxTimecode *) g_malloc (sizeof (Mpeg1MuxTimecode));
    tc->length = bpf;
    tc->original_length = tc->length;
    tc->frame_type = FRAME_TYPE_AUDIO;

    mb->current_start = offset + bpf;

    mb->info.audio.samples_per_second =
        (double) dfrequency[mb->info.audio.frequency];

    mb->info.audio.current_PTS =
        (mb->info.audio.decoding_order * samples[mb->info.audio.layer]) /
        mb->info.audio.samples_per_second * 90.;

    tc->DTS = tc->PTS = mb->info.audio.current_PTS;
    GST_DEBUG ("mpeg1mux::update_audio_info: PTS %" G_GUINT64_FORMAT ", %"
        G_GUINT64_FORMAT " length %lu", mb->info.audio.current_PTS, tc->PTS,
        tc->length);
    mb->timecode_list = g_list_append (mb->timecode_list, tc);

    mb->info.audio.decoding_order++;
    offset += tc->length;
  }
  mb->scan_pos = offset;
}
