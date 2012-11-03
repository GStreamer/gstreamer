/* 
 * Interplay MVE muxer plugin for GStreamer
 * Copyright (C) 2006 Jens Granseuer <jensgr@gmx.net>
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

#ifndef __GST_MVE_MUX_H__
#define __GST_MVE_MUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_MVE_MUX \
  (gst_mve_mux_get_type())
#define GST_MVE_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MVE_MUX,GstMveMux))
#define GST_MVE_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MVE_MUX,GstMveMux))
#define GST_IS_MVE_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MVE_MUX))
#define GST_IS_MVE_MUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MVE_MUX))


typedef struct _GstMveMux GstMveMux;
typedef struct _GstMveMuxClass GstMveMuxClass;

struct _GstMveMux {
  GstElement element;
  GMutex *lock;

  /* pads */
  GstPad *source;
  GstPad *videosink;
  GstPad *audiosink;

  gboolean audio_pad_connected;
  gboolean audio_pad_eos;
  gboolean video_pad_connected;
  gboolean video_pad_eos;

  guint64 stream_offset;
  /* audio stream time, really */
  GstClockTime stream_time;
  guint timer;
  gint state;

  /* ticks per frame */
  GstClockTime frame_duration;

  /* video stream properties */
  guint16 width, height;
  guint16 screen_width, screen_height;
  /* bits per pixel */
  guint8 bpp;
  /* previous frames */
  GstBuffer *last_frame;
  GstBuffer *second_last_frame;
  /* number of encoded frames */
  guint16 video_frames;
  /* palette handling */
  gboolean pal_changed;
  guint16 pal_first_color;
  guint16 pal_colors;
  /* whether to use expensive opcodes */
  gboolean quick_encoding;

  /* audio stream properties */
  /* bits per sample */
  guint8 bps;
  guint32 rate;
  guint8 channels;
  gboolean compression;
  /* current audio stream time */
  GstClockTime next_ts;
  /* maximum audio time we know about */
  GstClockTime max_ts;
  /* sample bytes per frame */
  guint16 spf;
  /* number of frames to use for audio lead-in */
  guint16 lead_frames;
  /* number of encoded frames */
  guint16 audio_frames;

  /* current chunk */
  guint8 *chunk_code_map;
  GByteArray *chunk_video;
  GByteArray *chunk_audio;
  gboolean chunk_has_palette;
  gboolean chunk_has_audio;

  /* buffers for incoming data */
  GQueue *audio_buffer;
  GQueue *video_buffer;
};

struct _GstMveMuxClass {
  GstElementClass parent_class;
};

GType gst_mve_mux_get_type (void);

GstFlowReturn mve_encode_frame8 (GstMveMux * mve,
    GstBuffer * frame, const guint32 * palette, guint16 max_data);
GstFlowReturn mve_encode_frame16 (GstMveMux * mve,
    GstBuffer * frame, guint16 max_data);
gint mve_compress_audio (guint8 * dest,
    const guint8 * src, guint16 len, guint8 channels);

G_END_DECLS

#endif /* __GST_MVE_MUX_H__ */
