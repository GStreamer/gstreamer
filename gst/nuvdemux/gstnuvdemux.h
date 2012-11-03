/* GStreamer
 * Copyright (C) <2006> Renato Araujo Oliveira Filho <renato.filho@indt.org.br>
 *                      Rosfran Borges <rosfran.borges@indt.org.br>
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

#ifndef __GST_NUV_DEMUX_H__
#define __GST_NUV_DEMUX_H__

#include <gst/gst.h>

#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_NUV_DEMUX \
  (gst_nuv_demux_get_type ())
#define GST_NUV_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_NUV_DEMUX, GstNuvDemux))
#define GST_NUV_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_NUV_DEMUX, GstNuvDemuxClass))
#define GST_IS_NUV_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_NUV_DEMUX))
#define GST_IS_NUV_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_NUV_DEMUX))


/* */
typedef struct
{
    gchar id[12];       /* "NuppelVideo\0" or "MythTVVideo\0" */
    gchar version[5];    /* "x.xx\0" */

    gint  i_width;
    gint  i_height;
    gint  i_width_desired;
    gint  i_height_desired;

    gchar i_mode;            /* P progressive, I interlaced */

    gdouble  d_aspect;       /* 1.0 squared pixel */
    gdouble  d_fps;

    gint     i_video_blocks; /* 0 no video, -1 unknown */
    gint     i_audio_blocks;
    gint     i_text_blocks;

    gint     i_keyframe_distance;

} nuv_header;

typedef struct
{
    gchar i_type;        /* A: audio, V: video, S: sync; T: test
                           R: Seekpoint (string:RTjjjjjjjj)
                           D: Extra data for codec */
    gchar i_compression; /* V: 0 uncompressed
                              1 RTJpeg
                              2 RTJpeg+lzo
                              N black frame
                              L copy last
                           A: 0 uncompressed (44100 1-bits, 2ch)
                              1 lzo
                              2 layer 2
                              3 layer 3
                              F flac
                              S shorten
                              N null frame loudless
                              L copy last
                            S: B audio and vdeo sync point
                               A audio sync info (timecode == effective
                                    dsp frequency*100)
                               V next video sync (timecode == next video
                                    frame num)
                               S audio,video,text correlation */
    gchar i_keyframe;    /* 0 keyframe, else no no key frame */
    guint8 i_filters;  /* 0x01: gauss 5 pixel (8,2,2,2,2)/16
                           0x02: gauss 5 pixel (8,1,1,1,1)/12
                           0x04: cartoon filter */

    gint i_timecode;     /* ms */

    gint i_length;       /* V,A,T: length of following data
                           S: length of packet correl */
} nuv_frame_header;

/* FIXME Not sure of this one */
typedef struct
{
    gint             i_version;
    guint32		     i_video_fcc;

    guint32		     i_audio_fcc;
    gint             i_audio_sample_rate;
    gint             i_audio_bits_per_sample;
    gint             i_audio_channels;
    gint             i_audio_compression_ratio;
    gint             i_audio_quality;
    gint             i_rtjpeg_quality;
    gint             i_rtjpeg_luma_filter;
    gint             i_rtjpeg_chroma_filter;
    gint             i_lavc_bitrate;
    gint             i_lavc_qmin;
    gint             i_lavc_qmax;
    gint             i_lavc_maxqdiff;
    gint64         	 i_seekable_offset;
    gint64           i_keyframe_adjust_offset;

} nuv_extended_header;

typedef enum {
  GST_NUV_DEMUX_START,
  GST_NUV_DEMUX_HEADER_DATA,
  GST_NUV_DEMUX_EXTRA_DATA,
  GST_NUV_DEMUX_MPEG_DATA,
  GST_NUV_DEMUX_EXTEND_HEADER,
  GST_NUV_DEMUX_EXTEND_HEADER_DATA,
  GST_NUV_DEMUX_FRAME_HEADER,
  GST_NUV_DEMUX_MOVI,
  GST_NUV_DEMUX_INVALID_DATA
} GstNuvDemuxState;

typedef struct _GstNuvDemux {
  GstElement     parent;

  guint         mode;
  GstAdapter    *adapter; 
  guint64       video_offset;
  guint64       audio_offset;

  /* pads */
  GstPad        *sinkpad;
  GstPad        *src_video_pad;  
  GstPad        *src_audio_pad;
  gboolean      first_video;
  gboolean      first_audio;

  /* NUV decoding state */
  GstNuvDemuxState state;
  guint64        offset;

  /* Mpeg ExtraData */
  guint64       mpeg_data_size;
  GstBuffer     *mpeg_buffer;
  
  nuv_header *h;
  nuv_extended_header *eh;
  nuv_frame_header *fh;
} GstNuvDemux;

typedef struct _GstNuvDemuxClass {
  GstElementClass parent_class;
} GstNuvDemuxClass;

GType           gst_nuv_demux_get_type          (void);

G_END_DECLS

#endif /* __GST_NUV_DEMUX_H__ */
