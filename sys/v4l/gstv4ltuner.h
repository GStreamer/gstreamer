/* GStreamer
 *
 * gstv4ltuner.h: tuner interface implementation for V4L
 *
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_V4L_TUNER_H__
#define __GST_V4L_TUNER_H__

#include <gst/gst.h>
#include <gst/tuner/tuner.h>

G_BEGIN_DECLS

#define GST_TYPE_V4L_TUNER_CHANNEL \
  (gst_v4l_tuner_channel_get_type ())
#define GST_V4L_TUNER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L_TUNER_CHANNEL, \
			       GstV4lTunerChannel))
#define GST_V4L_TUNER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_V4L_TUNER_CHANNEL, \
			    GstV4lTunerChannelClass))
#define GST_IS_V4L_TUNER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L_TUNER_CHANNEL))
#define GST_IS_V4L_TUNER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_V4L_TUNER_CHANNEL))

typedef struct _GstV4lTunerChannel {
  GstTunerChannel parent;

  gint            index;
  gint            tuner;
  gint            audio;
} GstV4lTunerChannel;

typedef struct _GstV4lTunerChannelClass {
  GstTunerChannelClass parent;
} GstV4lTunerChannelClass;

#define GST_TYPE_V4L_TUNER_NORM \
  (gst_v4l_tuner_norm_get_type ())
#define GST_V4L_TUNER_NORM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L_TUNER_NORM, \
			       GstV4lTunerNorm))
#define GST_V4L_TUNER_NORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_V4L_TUNER_NORM, \
			    GstV4lTunerNormClass))
#define GST_IS_V4L_TUNER_NORM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L_TUNER_NORM))
#define GST_IS_V4L_TUNER_NORM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_V4L_TUNER_NORM))

typedef struct _GstV4lTunerNorm {
  GstTunerNorm parent;

  gint         index;
} GstV4lTunerNorm;

typedef struct _GstV4lTunerNormClass {
  GstTunerNormClass parent;
} GstV4lTunerNormClass;

GType	gst_v4l_tuner_channel_get_type	(void);
GType	gst_v4l_tuner_norm_get_type	(void);

void	gst_v4l_tuner_interface_init	(GstTunerClass *klass);

#endif /* __GST_V4L_TUNER_H__ */
