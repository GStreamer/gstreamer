/* G-Streamer generic V4L2 element - Tuner interface implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstv4l2tuner.h: tuner interface implementation for V4L2
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

#ifndef __GST_V4L2_TUNER_H__
#define __GST_V4L2_TUNER_H__

#include <gst/gst.h>
#include <gst/tuner/tuner.h>

#include "gstv4l2element.h"

G_BEGIN_DECLS
#define GST_TYPE_V4L2_TUNER_CHANNEL \
  (gst_v4l2_tuner_channel_get_type ())
#define GST_V4L2_TUNER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_TUNER_CHANNEL, \
			       GstV4l2TunerChannel))
#define GST_V4L2_TUNER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_V4L2_TUNER_CHANNEL, \
			    GstV4l2TunerChannelClass))
#define GST_IS_V4L2_TUNER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_TUNER_CHANNEL))
#define GST_IS_V4L2_TUNER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_V4L2_TUNER_CHANNEL))
    typedef struct _GstV4l2TunerChannel
{
  GstTunerChannel parent;

  guint32 index;
  guint32 tuner;
  guint32 audio;
} GstV4l2TunerChannel;

typedef struct _GstV4l2TunerChannelClass
{
  GstTunerChannelClass parent;
} GstV4l2TunerChannelClass;

#define GST_TYPE_V4L2_TUNER_NORM \
  (gst_v4l2_tuner_norm_get_type ())
#define GST_V4L2_TUNER_NORM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_TUNER_NORM, \
			       GstV4l2TunerNorm))
#define GST_V4L2_TUNER_NORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_V4L2_TUNER_NORM, \
			    GstV4l2TunerNormClass))
#define GST_IS_V4L2_TUNER_NORM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_TUNER_NORM))
#define GST_IS_V4L2_TUNER_NORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_V4L2_TUNER_NORM))

typedef struct _GstV4l2TunerNorm
{
  GstTunerNorm parent;

  v4l2_std_id index;
} GstV4l2TunerNorm;

typedef struct _GstV4l2TunerNormClass
{
  GstTunerNormClass parent;
} GstV4l2TunerNormClass;

GType gst_v4l2_tuner_channel_get_type (void);
GType gst_v4l2_tuner_norm_get_type (void);

void gst_v4l2_tuner_interface_init (GstTunerClass * klass);

#endif /* __GST_V4L2_TUNER_H__ */
