/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstsegment.h: Header for GstSegment subsystem
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


#ifndef __GST_SEGMENT_H__
#define __GST_SEGMENT_H__

#include <gst/gstformat.h>

G_BEGIN_DECLS

#define GST_TYPE_SEGMENT             (gst_segment_get_type())

typedef struct _GstSegment GstSegment;

/**
 * GstSeekType:
 * @GST_SEEK_TYPE_NONE: no change in position is required
 * @GST_SEEK_TYPE_CUR: change relative to currently configured segment. This
 *    can't be used to seek relative to the current playback position - do a
 *    position query, calculate the desired position and then do an absolute
 *    position seek instead if that's what you want to do.
 * @GST_SEEK_TYPE_SET: absolute position is requested
 * @GST_SEEK_TYPE_END: relative position to duration is requested
 *
 * The different types of seek events. When constructing a seek event with
 * gst_event_new_seek() or when doing gst_segment_do_seek ().
 */
typedef enum {
  /* one of these */
  GST_SEEK_TYPE_NONE            = 0,
  GST_SEEK_TYPE_CUR             = 1,
  GST_SEEK_TYPE_SET             = 2,
  GST_SEEK_TYPE_END             = 3
} GstSeekType;

/**
 * GstSeekFlags:
 * @GST_SEEK_FLAG_NONE: no flag
 * @GST_SEEK_FLAG_FLUSH: flush pipeline
 * @GST_SEEK_FLAG_ACCURATE: accurate position is requested, this might
 *                     be considerably slower for some formats.
 * @GST_SEEK_FLAG_KEY_UNIT: seek to the nearest keyframe. This might be
 *                     faster but less accurate.
 * @GST_SEEK_FLAG_SEGMENT: perform a segment seek.
 * @GST_SEEK_FLAG_SKIP: when doing fast foward or fast reverse playback, allow
 *                     elements to skip frames instead of generating all
 *                     frames. Since 0.10.22.
 *
 * Flags to be used with gst_element_seek() or gst_event_new_seek(). All flags
 * can be used together.
 *
 * A non flushing seek might take some time to perform as the currently
 * playing data in the pipeline will not be cleared.
 *
 * An accurate seek might be slower for formats that don't have any indexes
 * or timestamp markers in the stream. Specifying this flag might require a
 * complete scan of the file in those cases.
 *
 * When performing a segment seek: after the playback of the segment completes,
 * no EOS will be emmited by the element that performed the seek, but a
 * #GST_MESSAGE_SEGMENT_DONE message will be posted on the bus by the element.
 * When this message is posted, it is possible to send a new seek event to
 * continue playback. With this seek method it is possible to perform seemless
 * looping or simple linear editing.
 *
 * When doing fast forward (rate > 1.0) or fast reverse (rate < -1.0) trickmode
 * playback, the @GST_SEEK_FLAG_SKIP flag can be used to instruct decoders
 * and demuxers to adjust the playback rate by skipping frames. This can improve
 * performance and decrease CPU usage because not all frames need to be decoded.
 *
 * Also see part-seeking.txt in the GStreamer design documentation for more
 * details on the meaning of these flags and the behaviour expected of
 * elements that handle them.
 */
typedef enum {
  GST_SEEK_FLAG_NONE            = 0,
  GST_SEEK_FLAG_FLUSH           = (1 << 0),
  GST_SEEK_FLAG_ACCURATE        = (1 << 1),
  GST_SEEK_FLAG_KEY_UNIT        = (1 << 2),
  GST_SEEK_FLAG_SEGMENT         = (1 << 3),
  GST_SEEK_FLAG_SKIP            = (1 << 4)
} GstSeekFlags;

/**
 * GstSegmentFlags:
 * @GST_SEGMENT_FLAG_NONE: no flags
 * @GST_SEGMENT_FLAG_RESET: reset the pipeline running_time to the segment
 *                          running_time
 * @GST_SEGMENT_FLAG_SKIP: perform skip playback
 *
 * Flags for the GstSegment structure. Currently mapped to the corresponding
 * values of the seek flags.
 */
typedef enum {
  GST_SEGMENT_FLAG_NONE            = GST_SEEK_FLAG_NONE,
  GST_SEGMENT_FLAG_RESET           = GST_SEEK_FLAG_FLUSH,
  GST_SEGMENT_FLAG_SKIP            = GST_SEEK_FLAG_SKIP
} GstSegmentFlags;

/**
 * GstSegment:
 * @flags: flags for this segment
 * @rate: the rate of the segment
 * @applied_rate: the already applied rate to the segment
 * @format: the format of the segment values
 * @base: the base time of the segment
 * @start: the start of the segment
 * @stop: the stop of the segment
 * @time: the stream time of the segment
 *
 * A helper structure that holds the configured region of
 * interest in a media file.
 */
struct _GstSegment {
  /*< public >*/
  GstSegmentFlags flags;

  gdouble         rate;
  gdouble         applied_rate;

  GstFormat       format;
  guint64         base;
  guint64         start;
  guint64         stop;
  guint64         time;

  guint64         position;
  guint64         duration;
};

GType        gst_segment_get_type            (void);

GstSegment * gst_segment_new                 (void);
GstSegment * gst_segment_copy                (const GstSegment *segment);
void         gst_segment_copy_into           (const GstSegment *src, GstSegment *dest);
void         gst_segment_free                (GstSegment *segment);

void         gst_segment_init                (GstSegment *segment, GstFormat format);

guint64      gst_segment_to_stream_time      (const GstSegment *segment, GstFormat format, guint64 position);
guint64      gst_segment_to_running_time     (const GstSegment *segment, GstFormat format, guint64 position);
guint64      gst_segment_to_position         (const GstSegment *segment, GstFormat format, guint64 running_time);

gboolean     gst_segment_set_running_time    (GstSegment *segment, GstFormat format, guint64 running_time);

gboolean     gst_segment_clip                (const GstSegment *segment, GstFormat format, guint64 start,
                                              guint64 stop, guint64 *clip_start, guint64 *clip_stop);


gboolean     gst_segment_do_seek             (GstSegment * segment, gdouble rate,
                                              GstFormat format, GstSeekFlags flags,
                                              GstSeekType start_type, guint64 start,
                                              GstSeekType stop_type, guint64 stop, gboolean * update);

G_END_DECLS

#endif /* __GST_SEGMENT_H__ */
