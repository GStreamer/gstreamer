/* GStreamer
 *
 * Copyright (c) 2008,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (c) 2008-2017 Collabora Ltd
 *  @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *  @author: Vincent Penquerc'h <vincent.penquerch@collabora.com>
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

#ifndef __GST_FLV_MUX_H__
#define __GST_FLV_MUX_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>

G_BEGIN_DECLS

#define LEGACY_FLV_VIDEO_CAPS "video/x-flash-video; \
        video/x-flash-screen; \
        video/x-vp6-flash; video/x-vp6-alpha; \
        video/x-h264, stream-format=avc;"

#define LEGACY_FLV_AUDIO_CAPS "audio/x-adpcm, layout = (string) swf, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; \
        audio/mpeg, mpegversion = (int) 1, layer = (int) 3, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 22050, 44100 }, parsed = (boolean) TRUE; \
        audio/mpeg, mpegversion = (int) { 4, 2 }, stream-format = (string) raw; \
        audio/x-nellymoser, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 16000, 22050, 44100 }; \
        audio/x-raw, format = (string) { U8, S16LE}, layout = (string) interleaved, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; \
        audio/x-alaw, channels = (int) { 1, 2 }, rate = (int) 8000; \
        audio/x-mulaw, channels = (int) { 1, 2 }, rate = (int) 8000; \
        audio/x-speex, channels = (int) 1, rate = (int) 16000;"

#define FLV_ENHANCED_VIDEO_CAPS "video/x-h265, stream-format=(string)hvc1, alignment=(string)au;"

#define GST_TYPE_FLV_MUX_PAD (gst_flv_mux_pad_get_type())
#define GST_FLV_MUX_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLV_MUX_PAD, GstFlvMuxPad))
#define GST_FLV_MUX_PAD_CAST(obj) ((GstFlvMuxPad *)(obj))
#define GST_FLV_MUX_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLV_MUX_PAD, GstFlvMuxPad))
#define GST_IS_FLV_MUX_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLV_MUX_PAD))
#define GST_IS_FLV_MUX_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLV_MUX_PAD))

typedef struct _GstFlvMuxPad GstFlvMuxPad;
typedef struct _GstFlvMuxPadClass GstFlvMuxPadClass;
typedef struct _GstFlvMux GstFlvMux;
typedef struct _GstFlvMuxClass GstFlvMuxClass;

#define GST_TYPE_FLV_MUX \
  (gst_flv_mux_get_type ())
#define GST_FLV_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FLV_MUX, GstFlvMux))
#define GST_FLV_MUX_CAST(obj) ((GstFlvMux *)obj)
#define GST_FLV_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FLV_MUX, GstFlvMuxClass))
#define GST_IS_FLV_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FLV_MUX))
#define GST_IS_FLV_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FLV_MUX))

typedef enum {
  GST_FLV_MUX_TRACK_TYPE_AUDIO = 1,
  GST_FLV_MUX_TRACK_TYPE_VIDEO = 2,
} GstFlvMuxTrackType;

/**
 * GstFlvTrackMode:
 * @GST_FLV_TRACK_MODE_ENHANCED_MULTITRACK: Stream the track with each FLV packet containing a Multitrack.OneTrack type. The track ID is always 0.
 * @GST_FLV_TRACK_MODE_ENHANCED_NON_MULTITRACK: Stream the track in enhanced FLV, but it is not Multitrack type packet, so there won't be a track ID.
 * @GST_FLV_TRACK_MODE_LEGACY: Stream the track in the legacy FLV format without any extended header.
 *
 * Since: 1.28
 */
typedef enum
{
  GST_FLV_TRACK_MODE_ENHANCED_MULTITRACK = 0,
  GST_FLV_TRACK_MODE_ENHANCED_NON_MULTITRACK = 1,
  GST_FLV_TRACK_MODE_LEGACY = 2,
} GstFlvTrackMode;

struct _GstFlvMuxPad
{
  GstAggregatorPad aggregator_pad;

  guint32 codec;
  // used to send in the legacy FLV header
  guint rate;
  guint width;
  guint channels;

  // used to send in the metadata
  gint audio_samplerate;
  gint audio_samplesize;
  gint audio_channels;

  gint video_width;
  gint video_height;
  gint video_framerate_n;
  gint video_framerate_d;
  gboolean video_have_framerate;
  gint video_par_n;
  gint video_par_d;
  gboolean video_have_par;

  GstBuffer *codec_data;

  guint bitrate;

  GstClockTime last_timestamp;
  GstClockTime pts;
  GstClockTime dts;

  gboolean info_changed;
  gboolean drop_deltas;
  gint16 track_id;
  GstFlvMuxTrackType type;
  guint8 flv_track_mode;
};

struct _GstFlvMuxPadClass {
  GstAggregatorPadClass parent;
};

typedef enum
{
  GST_FLV_MUX_STATE_HEADER,
  GST_FLV_MUX_STATE_DATA
} GstFlvMuxState;

struct _GstFlvMux {
  GstAggregator   aggregator;

  GstPad         *srcpad;

  /* <private> */
  GstFlvMuxState state;
  GList *audio_pads;
  GList *video_pads;
  gboolean streamable;
  gchar *metadatacreator;
  gchar *encoder;
  gboolean skip_backwards_streams;
  gboolean enforce_increasing_timestamps;

  GstTagList *tags;
  gboolean new_metadata;
  GList *index;
  guint64 byte_count;
  GstClockTime duration;
  GstClockTime first_timestamp;
  guint64 last_dts;

  gboolean sent_header;
  guint max_audio_pad_serial;
  guint max_video_pad_serial;
};

struct _GstFlvMuxClass {
  GstAggregatorClass parent;
};

GType    gst_flv_mux_pad_get_type(void);
GType    gst_flv_mux_get_type    (void);

G_END_DECLS

#endif /* __GST_FLV_MUX_H__ */
