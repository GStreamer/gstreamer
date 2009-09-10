/* GStreamer
 * Copyright (C) <2009> Руслан Ижбулатов <lrn1986 _at_ gmail _dot_ com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef __GST_VIDEO_MEASURE_H__
#define __GST_VIDEO_MEASURE_H__

#include <gst/video/gstvideofilter.h>

#define GST_EVENT_VIDEO_MEASURE "application/x-videomeasure"

GstEvent *gst_event_new_measured (guint64 framenumber, GstClockTime timestamp,
    const gchar *metric, const GValue *mean, const GValue *lowest,
    const GValue *highest);

#endif /* __GST_VIDEO_MEASURE_H__ */
