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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_SPEEX_DEC_H__
#define __GST_SPEEX_DEC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

#include <speex/speex.h>
#include <speex/speex_callbacks.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>

G_BEGIN_DECLS

#define GST_TYPE_SPEEX_DEC (gst_speex_dec_get_type())
G_DECLARE_FINAL_TYPE (GstSpeexDec, gst_speex_dec, GST, SPEEX_DEC,
    GstAudioDecoder)

struct _GstSpeexDec {
  GstAudioDecoder   element;

  void                  *state;
  SpeexStereoState      *stereo;
  const SpeexMode       *mode;
  SpeexHeader           *header;
  SpeexCallback         callback;
  SpeexBits             bits;

  gboolean              enh;

  gint                  frame_size;
  GstClockTime          frame_duration;
  guint64               packetno;

  GstBuffer             *streamheader;
  GstBuffer             *vorbiscomment;
};

G_END_DECLS

#endif /* __GST_SPEEX_DEC_H__ */
