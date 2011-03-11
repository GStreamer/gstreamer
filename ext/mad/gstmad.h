/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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


#ifndef __GST_MAD_H__
#define __GST_MAD_H__

#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <mad.h>

G_BEGIN_DECLS

#define GST_TYPE_MAD \
  (gst_mad_get_type())
#define GST_MAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MAD,GstMad))
#define GST_MAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MAD,GstMadClass))
#define GST_IS_MAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MAD))
#define GST_IS_MAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MAD))


typedef struct _GstMad GstMad;
typedef struct _GstMadClass GstMadClass;

struct _GstMad
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* state */
  struct mad_stream stream;
  struct mad_frame frame;
  struct mad_synth synth;
  guchar *tempbuffer;           /* temporary buffer to serve to mad */
  glong tempsize;               /* running count of temp buffer size */
  GstClockTime last_ts;
  guint64 base_byte_offset;
  guint64 bytes_consumed;       /* since the base_byte_offset */
  guint64 total_samples;        /* the number of samples since the sync point */

  gboolean in_error;            /* set when mad's in an error state */
  gboolean restart;
  gboolean discont;
  guint64 segment_start;
  GstSegment segment;
  gboolean need_newsegment;

  /* info */
  struct mad_header header;
  gboolean new_header;
  guint framecount;
  gint vbr_average;             /* average bitrate */
  guint64 vbr_rate;             /* average * framecount */

  gboolean half;
  gboolean ignore_crc;

  GstTagList *tags;

  /* negotiated format */
  gint rate, pending_rate;
  gint channels, pending_channels;
  gint times_pending;

  gboolean caps_set;            /* used to keep track of whether to change/update caps */
  GstIndex *index;
  gint index_id;

  gboolean check_for_xing;
  gboolean xing_found;

  gboolean framed;              /* whether there is a demuxer in front of us */

  GList *pending_events;

  /* reverse playback */
  GList *decode;
  GList *gather;
  GList *queued;
  gboolean process;
};

struct _GstMadClass
{
  GstElementClass parent_class;
};

GType                   gst_mad_get_type (void);

G_END_DECLS

#endif /* __GST_MAD_H__ */
