/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2001> Thomas Vander Stichele <thomas@apestaart.org>
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

#include <gst/gst.h>

#include <gst/audio/audioclock.h>

#ifndef __GST_AUDIO_AUDIO_H__
#define __GST_AUDIO_AUDIO_H__

G_BEGIN_DECLS

/* For people that are looking at this source: the purpose of these defines is
 * to make GstCaps a bit easier, in that you don't have to know all of the
 * properties that need to be defined. you can just use these macros. currently
 * (8/01) the only plugins that use these are the passthrough, speed, volume,
 * adder, and [de]interleave plugins. These are for convenience only, and do not
 * specify the 'limits' of GStreamer. you might also use these definitions as a
 * base for making your own caps, if need be.
 *
 * For example, to make a source pad that can output streams of either mono
 * float or any channel int:
 *
 *  template = gst_pad_template_new
 *    ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
 *    gst_caps_append(gst_caps_new ("sink_int",  "audio/x-raw-int",
 *                                  GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
 *                    gst_caps_new ("sink_float", "audio/x-raw-float",
 *                                  GST_AUDIO_FLOAT_PAD_TEMPLATE_PROPS)),
 *    NULL);
 *
 *  sinkpad = gst_pad_new_from_template(template, "sink");
 *
 * Andy Wingo, 18 August 2001
 * Thomas, 6 September 2002 */

#define GST_AUDIO_DEF_RATE 44100

#define GST_AUDIO_INT_PAD_TEMPLATE_CAPS \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
  "width = (int) { 8, 16, 24, 32 }, " \
  "depth = (int) [ 1, 32 ], " \
  "signed = (boolean) { true, false }" 


/* "standard" int audio is native order, 16 bit stereo. */
#define GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) 2, " \
  "endianness = (int) BYTE_ORDER, " \
  "width = (int) 16, " \
  "depth = (int) 16, " \
  "signed = (boolean) true" 

#define GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS \
  "audio/x-raw-float, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, " \
  "width = (int) { 32, 64 }, " \
  "buffer-frames = (int) [ 1, MAX]"

/* "standard" float audio is native order, 32 bit mono. */
#define GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS \
  "audio/x-raw-float, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) 1, " \
  "endianness = (int) BYTE_ORDER, " \
  "buffer-frames = (int) [ 1, MAX]"

/*
 * this library defines and implements some helper functions for audio
 * handling
 */

/* get byte size of audio frame (based on caps of pad */
int      gst_audio_frame_byte_size      (GstPad* pad);

/* get length in frames of buffer */
long     gst_audio_frame_length         (GstPad* pad, GstBuffer* buf);

/* get frame rate based on caps */
long     gst_audio_frame_rate           (GstPad *pad);

/* calculate length in seconds of audio buffer buf based on caps of pad */
double   gst_audio_length               (GstPad* pad, GstBuffer* buf);

/* calculate highest possible sample value based on capabilities of pad */
long     gst_audio_highest_sample_value (GstPad* pad);

/* check if the buffer size is a whole multiple of the frame size */
gboolean gst_audio_is_buffer_framed     (GstPad* pad, GstBuffer* buf);

/* functions useful for _getcaps functions */
typedef enum {
  GST_AUDIO_FIELD_RATE          = (1 << 0),
  GST_AUDIO_FIELD_CHANNELS      = (1 << 1),
  GST_AUDIO_FIELD_ENDIANNESS    = (1 << 2),
  GST_AUDIO_FIELD_WIDTH         = (1 << 3),
  GST_AUDIO_FIELD_DEPTH         = (1 << 4),
  GST_AUDIO_FIELD_SIGNED        = (1 << 5),
  GST_AUDIO_FIELD_BUFFER_FRAMES = (1 << 6)
} GstAudioFieldFlag;

void gst_audio_structure_set_int (GstStructure *structure, GstAudioFieldFlag flag);

G_END_DECLS

#endif /* __GST_AUDIO_AUDIO_H__ */
