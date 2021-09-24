/* GStreamer RealAudio demuxer
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_REAL_AUDIO_DEMUX_H__
#define __GST_REAL_AUDIO_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_REAL_AUDIO_DEMUX \
  (gst_real_audio_demux_get_type())
#define GST_REAL_AUDIO_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REAL_AUDIO_DEMUX,GstRealAudioDemux))
#define GST_REAL_AUDIO_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REAL_AUDIO_DEMUX,GstRealAudioDemuxClass))
#define GST_IS_REAL_AUDIO_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REAL_AUDIO_DEMUX))
#define GST_IS_REAL_AUDIO_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REAL_AUDIO_DEMUX))

typedef enum
{
  REAL_AUDIO_DEMUX_STATE_MARKER,
  REAL_AUDIO_DEMUX_STATE_HEADER,
  REAL_AUDIO_DEMUX_STATE_DATA
} GstRealAudioDemuxState;

typedef struct _GstRealAudioDemux GstRealAudioDemux;
typedef struct _GstRealAudioDemuxClass GstRealAudioDemuxClass;

struct _GstRealAudioDemux {
  GstElement               element;

  GstPad                  *sinkpad;
  GstPad                  *srcpad;
  
  gboolean                 have_group_id;
  guint                    group_id;

  GstAdapter              *adapter;
  GstRealAudioDemuxState   state;

  guint                    ra_version;
  guint                    data_offset;

  guint                    packet_size;
  guint                    leaf_size;
  guint                    height;
  guint                    flavour;

  guint                    sample_rate;
  guint                    sample_width;
  guint                    channels;
  guint32                  fourcc;

  gboolean                 segment_running;

  gboolean                 need_newsegment;
  GstTagList              *pending_tags;

  guint                    byterate_num;    /* bytes per second */
  guint                    byterate_denom;

  gint64                   duration;
  gint64                   upstream_size;

  guint64                  offset;          /* current read byte offset for
                                             * pull_range-based mode */

  /* playback start/stop positions */
  GstSegment               segment;

  gboolean                 seekable;
};

struct _GstRealAudioDemuxClass {
  GstElementClass  element_class;
};

GType  gst_real_audio_demux_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (rademux);

G_END_DECLS

#endif /* __GST_REAL_AUDIO_DEMUX_H__ */
