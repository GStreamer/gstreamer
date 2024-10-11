/* GStreamer H.265 Parser
 * Copyright (C) 2013 Intel Corporation
 *   Contact: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifndef __GST_H265_PARSE_H__
#define __GST_H265_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>
#include <gst/codecparsers/gsth265parser.h>
#include <gst/video/video.h>
#include "gstvideoparseutils.h"

G_BEGIN_DECLS

#define GST_TYPE_H265_PARSE \
  (gst_h265_parse_get_type())
#define GST_H265_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H265_PARSE,GstH265Parse))
#define GST_H265_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H265_PARSE,GstH265ParseClass))
#define GST_IS_H265_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H265_PARSE))
#define GST_IS_H265_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H265_PARSE))

GType gst_h265_parse_get_type (void);

typedef struct _GstH265Parse GstH265Parse;
typedef struct _GstH265ParseClass GstH265ParseClass;

struct _GstH265Parse
{
  GstBaseParse baseparse;

  /* stream */
  gint width, height;
  gint fps_num, fps_den;
  gint upstream_par_n, upstream_par_d;
  gint parsed_par_n, parsed_par_d;
  gint parsed_fps_n, parsed_fps_d;
  GstVideoColorimetry parsed_colorimetry;
  /* current codec_data in output caps, if any */
  GstBuffer *codec_data;
  /* input codec_data, if any */
  GstBuffer *codec_data_in;
  guint nal_length_size;
  gboolean packetized;
  gboolean split_packetized;
  gboolean transform;

  /* state */
  GstH265Parser *nalparser;
  guint in_align;
  guint state;
  guint align;
  guint format;
  gint current_off;

  GstClockTime last_report;
  gboolean push_codec;
  /* The following variables have a meaning in context of "have
   * VPS/SPS/PPS to push downstream", e.g. to update caps */
  gboolean have_vps;
  gboolean have_sps;
  gboolean have_pps;

  /* per frame vps/sps/pps check for periodic push codec decision */
  gboolean have_vps_in_frame;
  gboolean have_sps_in_frame;
  gboolean have_pps_in_frame;

  gboolean first_frame;

  /* collected SPS and PPS NALUs */
  GstBuffer *vps_nals[GST_H265_MAX_VPS_COUNT];
  GstBuffer *sps_nals[GST_H265_MAX_SPS_COUNT];
  GstBuffer *pps_nals[GST_H265_MAX_PPS_COUNT];

  /* Infos we need to keep track of */
  guint8 sei_pic_struct;

  /* Collected TimeCode SEI */
  GstH265TimeCode time_code;

  gboolean discont;
  gboolean marker;

  /* frame parsing */
  gint idr_pos, sei_pos;
  gboolean update_caps;
  GstAdapter *frame_out;
  gboolean keyframe;
  gboolean predicted;
  gboolean bidirectional;
  gboolean header;
  gboolean framerate_from_caps;
  /* AU state */
  gboolean picture_start;

  GstVideoParseUserData user_data;
  GstVideoParseUserDataUnregistered user_data_unregistered;

  /* props */
  gint interval;

  GstClockTime pending_key_unit_ts;
  GstEvent *force_key_unit_event;

  GstVideoMasteringDisplayInfo mastering_display_info;
  guint mastering_display_info_state;

  GstVideoContentLightLevel content_light_level;
  guint content_light_level_state;

  /* For forward predicted trickmode */
  gboolean discard_bidirectional;
};

struct _GstH265ParseClass
{
  GstBaseParseClass parent_class;
};

G_END_DECLS
#endif
