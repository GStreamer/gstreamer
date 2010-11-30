/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
#ifndef _GST_VIDEO_RECORDING_BIN_H_
#define _GST_VIDEO_RECORDING_BIN_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_RECORDING_BIN   (gst_video_recording_bin_get_type())
#define GST_VIDEO_RECORDING_BIN(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_RECORDING_BIN,GstVideoRecordingBin))
#define GST_VIDEO_RECORDING_BIN_CAST(obj)   ((GstVideoRecordingBin *) obj)
#define GST_VIDEO_RECORDING_BIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_RECORDING_BIN,GstVideoRecordingBinClass))
#define GST_IS_VIDEO_RECORDING_BIN(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_RECORDING_BIN))
#define GST_IS_VIDEO_RECORDING_BIN_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_RECORDING_BIN))

typedef struct _GstVideoRecordingBin GstVideoRecordingBin;
typedef struct _GstVideoRecordingBinClass GstVideoRecordingBinClass;

struct _GstVideoRecordingBin
{
  GstBin bin;

  GstPad *ghostpad;
  GstElement *sink;

  /* props */
  gchar *location;

  gboolean elements_created;
};

struct _GstVideoRecordingBinClass
{
  GstBinClass bin_class;
};

GType gst_video_recording_bin_get_type (void);
gboolean gst_video_recording_bin_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif
