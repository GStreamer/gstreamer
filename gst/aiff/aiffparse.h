/* GStreamer
 * Copyright (C) <2008> Pioneers of the Inevitable <songbird@songbirdnest.com>
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


#ifndef __GST_AIFFPARSE_H__
#define __GST_AIFFPARSE_H__


#include <gst/gst.h>
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-read.h"
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define TYPE_AIFFPARSE \
  (gst_aiffparse_get_type())
#define AIFFPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),TYPE_AIFFPARSE,AIFFParse))
#define AIFFPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),TYPE_AIFFPARSE,AIFFParseClass))
#define IS_AIFFPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),TYPE_AIFFPARSE))
#define IS_AIFFPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),TYPE_AIFFPARSE))

typedef enum {
  AIFFPARSE_START,
  AIFFPARSE_HEADER,
  AIFFPARSE_DATA
} AIFFParseState;

typedef struct _AIFFParse AIFFParse;
typedef struct _AIFFParseClass AIFFParseClass;

/**
 * AIFFParse:
 *
 * Opaque data structure.
 */
struct _AIFFParse {
  GstElement parent;

  /* pads */
  GstPad *sinkpad,*srcpad;

  GstCaps     *caps;
  GstEvent    *close_segment;
  GstEvent    *start_segment;

  /* AIFF decoding state */
  AIFFParseState state;

  /* format of audio, see defines below */
  gint format;

  gboolean is_aifc;

  /* useful audio data */
  guint32 rate;
  guint16 channels;
  guint16 width;
  guint16 depth;
  guint32 endianness;

  /* real bytes per second used or 0 when no bitrate is known */
  guint32 bps;

  guint bytes_per_sample;

  guint32   total_frames;

  guint32 ssnd_offset;
  guint32 ssnd_blocksize;

  /* position in data part */
  guint64	offset;
  guint64	end_offset;
  guint64 	dataleft;
  /* offset/length of data part */
  guint64 	datastart;
  guint64 	datasize;
  /* duration in time */
  guint64 	duration;

  /* pending seek */
  GstEvent *seek_event;

  /* For streaming */
  GstAdapter *adapter;
  gboolean got_comm;
  gboolean streaming;

  /* configured segment, start/stop expressed in time */
  GstSegment segment;
  gboolean segment_running;

  /* discont after seek */
  gboolean discont;
};

struct _AIFFParseClass {
  GstElementClass parent_class;
};

GType aiffparse_get_type(void);

G_END_DECLS

#endif /* __GST_AIFFPARSE_H__ */
