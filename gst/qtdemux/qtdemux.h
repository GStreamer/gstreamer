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


#ifndef __GST_QTDEMUX_H__
#define __GST_QTDEMUX_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_QTDEMUX \
  (gst_qtdemux_get_type())
#define GST_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTDEMUX,GstQTDemux))
#define GST_QTDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTDEMUX,GstQTDemux))
#define GST_IS_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTDEMUX))
#define GST_IS_QTDEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTDEMUX))

#define GST_QTDEMUX_MAX_AUDIO_PADS	8	
#define GST_QTDEMUX_MAX_VIDEO_PADS	8	

  /*
   * smartass macross that turns guint32 into sequence of bytes separated with comma
   * to be used in printf("%c%c%c%c",GST_FOURCC_TO_CHARSEQ(fourcc)) fashion
   */
#define GST_FOURCC_TO_CHARSEQ(f) f&0xff, (f>>8)&0xff , (f>>16)&0xff, f>>24

typedef struct _GstQTDemux GstQTDemux;
typedef struct _GstQTDemuxClass GstQTDemuxClass;

typedef struct {
  guint64 start;
  guint64 size; /* if 0, lasts till the end of file */
  guint32 type;
} GstQtpAtom;

typedef struct {
  guint32 size;
  guint32 type;
} GstQtpAtomMinHeader;

#define GST_QTP_CONTAINER_ATOM 1

typedef void GstQtpAtomTypeHandler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);

typedef struct {
  guint32 flags;
  GstQtpAtomTypeHandler * handler;
} GstQtpAtomType;

typedef struct {
  guint64 offset;
  guint32 size;
  guint32 timestamp;
  struct _GstQtpTrack * track;
} GstQtpSample;

typedef struct _GstQtpTrack {
  guint32 format;
  guint32 width;
  guint32 height;
  guint32 time_scale; /* units per second */
  guint32 sample_duration; /* units in sample */

  /* temporary buffers with sample tables */
  GstBuffer * stsd, * stts, * stsc, * stsz, * stco;

  /* this track samples in array */
  GstQtpSample * samples;

  GstPad * pad;
} GstQtpTrack;

typedef struct {
  guint32 size;
  guint32 format;
  char reserved[6];
  guint16 dataref;
} __attribute__((packed)) /* FIXME may it wasn't necessary? */ GstQtpStsdRec;

typedef struct {
  guint32 size;
  guint32 format;
  char reserved[6];
  guint16 dataref;
  guint16 version;
  guint16 rev_level;
  guint32 vendor;
  guint32 temporal_quality;
  guint32 spatial_quality;
  guint16 width;
  guint16 height;
  guint32 hres;
  guint32 vres;
  guint32 data_size;
  guint16 frame_count; /* frames per sample */
  guint32 compressor_name;
  guint16 depth;
  guint16 color_table_id;
} __attribute__((packed)) /* FIXME may it wasn't necessary? */ GstQtpStsdVideRec;

typedef struct {
  guint32 count;
  guint32 duration;
} GstQtpSttsRec;

typedef struct {
  guint32 first_chunk;
  guint32 samples_per_chunk;
  guint32 sample_desc;
} GstQtpStscRec;

struct _GstQTDemux {
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *video_pad[GST_QTDEMUX_MAX_VIDEO_PADS];
  int num_video_pads;

  GstByteStream *bs;
  guint64 bs_pos; /* current position in bs (coz bs won't tell) */

  /* 
   * nesting stack: everytime parser reads a header
   * of a container atom it is added to the stack until 
   * and removed whenever it's over (read completely)
   */
  GSList * nested;
  int nested_cnt; /* just to make it simpler */

  GList * tracks;
  GTree * samples; /* samples of all the tracks ordered by the offset */
};

struct _GstQTDemuxClass {
  GstElementClass parent_class;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_QTDEMUX_H__ */
