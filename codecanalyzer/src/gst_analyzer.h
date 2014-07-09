/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef __GST_ANALYZER__
#define __GST_ANALYZER__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

typedef struct _GstAnalyzer GstAnalyzer;
typedef struct _GstAnalyzerVideoInfo GstAnalyzerVideoInfo;

typedef enum {
  GST_ANALYZER_STATUS_SUCCESS = 0,
  GST_ANALYZER_STATUS_CODEC_PARSER_MISSING = 1,
  GST_ANALYZER_STATUS_CODEC_NOT_SUPPORTED = 2,
  GST_ANALYZER_STATUS_STREAM_FORMAT_UNKNOWN = 3,
  GST_ANALYZER_STATUS_ERROR_UNKNOWN = 4
} GstAnalyzerStatus;

const gchar* gst_analyzer_status_get_name       (GstAnalyzerStatus status);

typedef enum {
  GST_ANALYZER_CODEC_UNKNOWN = 0,
  GST_ANALYZER_CODEC_MPEG2_VIDEO = 1,
  GST_ANALYZER_CODEC_H264 = 2,
  GST_ANALYZER_CODEC_VC1 = 3,
  GST_ANALYZER_CODEC_MPEG4_PART_TWO = 4,
  GST_ANALYZER_CODEC_H265 = 5,
  GST_ANALYZER_CODEC_VP8 = 6,
  GST_ANALYZER_CODEC_VP9 = 7
} GstAnalyzerCodecType;

struct _GstAnalyzerVideoInfo
{
  gchar *codec_name;
  guint width;
  guint height;
  guint depth;
  guint avg_bitrate;
  guint max_bitrate;
  guint fps_n;
  guint fps_d;
  guint par_n;
  guint par_d;
};

struct _GstAnalyzer
{
  GstAnalyzerVideoInfo *video_info;

  gchar *codec_name;

  GstElement *pipeline;
  GstElement *src;
  GstElement *parser;
  GstElement *sink;

  guint bus_watch_id;

  gboolean complete_analyze;
  gint NumOfFramesToAnalyze;
  gint NumOfAnalyzedFrames;
};

GstAnalyzerStatus gst_analyzer_init (GstAnalyzer *analyzer, char *uri);

void gst_analyzer_set_file_name (GstAnalyzer *analyzer,
				 char *file_name);

void gst_analyzer_set_destination_dir_path (GstAnalyzer * analyzer,
					    char *uri);

void gst_analyzer_set_num_frames (GstAnalyzer *analyzer,
				  gint num_frames);

gboolean gst_analyzer_start (GstAnalyzer *analyzer);

gboolean gst_analyzer_stop (GstAnalyzer *analyzer);

void gst_analyzer_destory (GstAnalyzer *analyzer);

GstAnalyzerVideoInfo *gst_analyzer_video_info_new ();

gboolean gst_analyzer_video_info_from_uri (GstAnalyzerVideoInfo *vinfo, gchar *uri);

void gst_analyzer_video_info_destroy (GstAnalyzerVideoInfo *video_info);

#endif /* __GST_ANALYZER__ */
