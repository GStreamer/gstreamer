/* GStreamer
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_VIDEO_H__
#define __GST_VIDEO_H__

#include <gst/gst.h>
#include <gst/video/video-event.h>
#include <gst/video/video-format.h>
#include <gst/video/video-color.h>
#include <gst/video/video-info.h>
#include <gst/video/video-frame.h>
#include <gst/video/video-enumtypes.h>

G_BEGIN_DECLS

/* some helper functions */
gboolean       gst_video_calculate_display_ratio (guint * dar_n,
                                                  guint * dar_d,
                                                  guint   video_width,
                                                  guint   video_height,
                                                  guint   video_par_n,
                                                  guint   video_par_d,
                                                  guint   display_par_n,
                                                  guint   display_par_d);

/* convert/encode video sample from one format to another */

typedef void (*GstVideoConvertSampleCallback) (GstSample * sample, GError *error, gpointer user_data);

void          gst_video_convert_sample_async (GstSample                    * sample,
                                              const GstCaps                * to_caps,
                                              GstClockTime                   timeout,
                                              GstVideoConvertSampleCallback  callback,
                                              gpointer                       user_data,
                                              GDestroyNotify                 destroy_notify);

GstSample *   gst_video_convert_sample       (GstSample     * sample,
                                              const GstCaps * to_caps,
                                              GstClockTime    timeout,
                                              GError       ** error);
G_END_DECLS

#endif /* __GST_VIDEO_H__ */
