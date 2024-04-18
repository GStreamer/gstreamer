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

#ifndef __GST_MPG123_AUDIO_DEC_H__
#define __GST_MPG123_AUDIO_DEC_H__

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/audio/gstaudiodecoder.h>
#include <mpg123.h>


G_BEGIN_DECLS

#define GST_TYPE_MPG123_AUDIO_DEC (gst_mpg123_audio_dec_get_type())
G_DECLARE_FINAL_TYPE (GstMpg123AudioDec, gst_mpg123_audio_dec,
    GST, MPG123_AUDIO_DEC, GstAudioDecoder)

struct _GstMpg123AudioDec
{
  GstAudioDecoder parent;

  mpg123_handle *handle;

  GstAudioInfo next_audioinfo;
  gboolean has_next_audioinfo;

  off_t frame_offset;

  GstVecDeque *audio_clip_info_queue;
};

GST_ELEMENT_REGISTER_DECLARE (mpg123audiodec);

G_END_DECLS

#endif
