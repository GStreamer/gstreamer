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


#ifndef __MP3PARSE_H__
#define __MP3PARSE_H__


#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_MP3PARSE \
  (gst_mp3parse_get_type())
#define GST_MP3PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MP3PARSE,GstMPEGAudioParse))
#define GST_MP3PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MP3PARSE,GstMPEGAudioParseClass))
#define GST_IS_MP3PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MP3PARSE))
#define GST_IS_MP3PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MP3PARSE))

typedef struct _GstMPEGAudioParse GstMPEGAudioParse;
typedef struct _GstMPEGAudioParseClass GstMPEGAudioParseClass;
typedef struct _MPEGAudioSeekEntry MPEGAudioSeekEntry;
typedef struct _MPEGAudioPendingAccurateSeek MPEGAudioPendingAccurateSeek;


struct _MPEGAudioSeekEntry {
  gint64 byte;
  GstClockTime timestamp;
};

struct _MPEGAudioPendingAccurateSeek {
  GstSegment segment;
  gint64 upstream_start;
  GstClockTime timestamp_start;
};

struct _GstMPEGAudioParse {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstSegment segment;
  GstClockTime next_ts;
  gboolean discont;

  /* Offset as supplied by incoming buffers */
  gint64 cur_offset;

  /* Upcoming timestamp given on an incoming buffer and
   * the offset at which it becomes active */
  GstClockTime pending_ts;
  gint64 pending_offset;
  /* Offset since the last newseg */
  gint64 tracked_offset;
  /* tracked_offset when resyncing started */
  gint64 sync_offset;

  GstAdapter *adapter;

  guint skip; /* number of frames to skip */
  guint bit_rate; /* in kbps */
  gint channels, rate, layer, version;
  GstClockTime max_bitreservoir;
  gint spf; /* Samples per frame */

  gboolean resyncing; /* True when attempting to resync (stricter checks are
                         performed) */
  gboolean sent_codec_tag;

  /* VBR tracking */
  guint   avg_bitrate;
  guint64 bitrate_sum;
  guint   frame_count;
  guint   last_posted_bitrate;
  gint    last_posted_crc;
  guint   last_posted_channel_mode;

  /* Xing info */
  guint32 xing_flags;
  guint32 xing_frames;
  GstClockTime xing_total_time;
  guint32 xing_bytes;
  /* percent -> filepos mapping */
  guchar xing_seek_table[100];
  /* filepos -> percent mapping */
  guint16 xing_seek_table_inverse[256];
  guint32 xing_vbr_scale;
  guint   xing_bitrate;

  /* VBRI info */
  guint32 vbri_frames;
  GstClockTime vbri_total_time;
  guint32 vbri_bytes;
  guint vbri_bitrate;
  guint vbri_seek_points;
  guint32 *vbri_seek_table;
  gboolean vbri_valid;

  /* Accurate seeking */
  GList *seek_table;
  GMutex *pending_seeks_lock;
  GSList *pending_accurate_seeks;
  gboolean exact_position;

  GSList *pending_nonaccurate_seeks;

  /* Track whether we're seekable (in BYTES format, if upstream operates in
   * TIME format, we don't care about seekability and assume upstream handles
   * it). The seek table for accurate seeking is not maintained if we're not
   * seekable. */
  gboolean seekable;

  /* minimum distance between two index entries */
  GstClockTimeDiff idx_interval;

  /* pending segment */
  GstEvent *pending_segment;
  /* pending events */
  GList *pending_events;
};

struct _GstMPEGAudioParseClass {
  GstElementClass parent_class;
};

GType gst_mp3parse_get_type(void);

G_END_DECLS

#endif /* __MP3PARSE_H__ */
