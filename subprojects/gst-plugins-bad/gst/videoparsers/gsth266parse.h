/* GStreamer H.266 Parse
 *
 * Copyright (C) 2024 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
 *    Author: Zhong Hongcheng <spartazhc@gmail.com>
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

#ifndef __GST_H266_PARSE_H__
#define __GST_H266_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>
#include <gst/codecparsers/gsth266parser.h>
#include <gst/video/video.h>
#include "gstvideoparseutils.h"

G_BEGIN_DECLS

#define GST_TYPE_H266_PARSE \
  (gst_h266_parse_get_type())
#define GST_H266_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H266_PARSE,GstH266Parse))
#define GST_H266_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H266_PARSE,GstH266ParseClass))
#define GST_IS_H266_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H266_PARSE))
#define GST_IS_H266_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H266_PARSE))

GType gst_h266_parse_get_type (void);

typedef struct _GstH266Parse GstH266Parse;
typedef struct _GstH266ParseClass GstH266ParseClass;
typedef struct _GstH266ParsePrivate GstH266ParsePrivate;

/**
 * GstH266Parse:
 *
 * The H266 NAL Parse
 *
 * Since: 1.26
 */
struct _GstH266Parse
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
  GstH266Parser *nalparser;
  guint in_align;
  guint state;
  guint align;
  guint format;
  gint current_off;

  GstClockTime last_report;
  gboolean push_codec;
  /* The following variables have a meaning in context of "have
   * VPS/SPS/PPS/APS to push downstream", e.g. to update caps */
  gboolean have_vps;
  gboolean have_sps;
  gboolean have_pps;

  /* per frame vps/sps/pps/aps check for periodic push codec decision */
  gboolean have_vps_in_frame;
  gboolean have_sps_in_frame;
  gboolean have_pps_in_frame;

  gboolean first_frame;

  /* collected VPS, SPS, PPS and APS NALUs */
  GstBuffer *vps_nals[GST_H266_MAX_VPS_COUNT];
  GstBuffer *sps_nals[GST_H266_MAX_SPS_COUNT];
  GstBuffer *pps_nals[GST_H266_MAX_PPS_COUNT];

  /* FFI SEI Info we need to keep track of */
  GstH266FrameFieldInfo sei_frame_field;
  guint interlaced_mode;

  gboolean discont;
  gboolean marker;

  /* frame parsing */
  /* the pos to begin a new IDR, may include prefix SEI, APS, etc */
  gint idr_pos;
  gboolean update_caps;
  GstAdapter *frame_out;
  gboolean keyframe;
  gboolean predicted;
  gboolean bidirectional;
  gboolean header;
  gboolean framerate_from_caps;
  /* AU state */
  gboolean picture_start;
  guint last_nuh_layer_id;

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

  /*< private >*/
  GstH266ParsePrivate *priv;
  gpointer padding[GST_PADDING_LARGE];
};

struct _GstH266ParseClass
{
  GstBaseParseClass parent_class;
};

G_END_DECLS
#endif
