/* AVI muxer plugin for GStreamer
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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


#ifndef __GST_AVIMUX_H__
#define __GST_AVIMUX_H__


#include <gst/gst.h>
#include <gst/riff/riff-ids.h>
#include "avi-ids.h"


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_AVIMUX \
  (gst_avimux_get_type())
#define GST_AVIMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AVIMUX,GstAviMux))
#define GST_AVIMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AVIMUX,GstAviMux))
#define GST_IS_AVIMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AVIMUX))
#define GST_IS_AVIMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AVIMUX))


  typedef struct _GstAviMux GstAviMux;
  typedef struct _GstAviMuxClass GstAviMuxClass;

  struct _GstAviMux
  {
    GstElement element;

    /* pads */
    GstPad *srcpad;
    GstPad *audiosinkpad;
    gboolean audio_pad_connected, audio_pad_eos;
    GstPad *videosinkpad;
    gboolean video_pad_connected, video_pad_eos;

    /* the AVI header */
    gst_riff_avih avi_hdr;
    guint32 total_frames;	/* total number of frames */
    guint64 total_data;		/* amount of total data */
    guint32 data_size, datax_size;	/* amount of data (bytes) in the AVI/AVIX block */
    guint32 num_frames, numx_frames;	/* num frames in the AVI/AVIX block */
    guint32 header_size;
    gboolean write_header;
    gboolean restart;
    guint32 audio_size;
    guint64 audio_time;

    /* video header */
    gst_riff_strh vids_hdr;
    gst_riff_strf_vids vids;

    /* audio header */
    gst_riff_strh auds_hdr;
    gst_riff_strf_auds auds;

    /* information about the AVI index ('idx') */
    gst_riff_index_entry *idx;
    gint idx_index, idx_count;
    guint32 idx_offset, idx_size;

    /* are we a big file already? */
    gboolean is_bigfile;
    guint64 avix_start;

    /* whether to use "large AVI files" or just stick to small indexed files */
    gboolean enable_large_avi;

    /* in order to be usable as a loopbased element, we need an internal
     * 'buffered' buffer for each pad, so one for audio, one for video */
    GstBuffer *audio_buffer_queue, *video_buffer_queue;
  };

  struct _GstAviMuxClass
  {
    GstElementClass parent_class;
  };

  GType gst_avimux_get_type (void);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_AVIMUX_H__ */
