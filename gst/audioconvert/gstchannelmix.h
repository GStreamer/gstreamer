/* GStreamer
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstchannelmix.h: setup of channel conversion matrices
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

#ifndef __GST_CHANNEL_MIX_H__
#define __GST_CHANNEL_MIX_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/multichannel.h>

#define GST_TYPE_AUDIO_CONVERT          (gst_audio_convert_get_type())
#define GST_AUDIO_CONVERT(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_CONVERT,GstAudioConvert))
#define GST_AUDIO_CONVERT_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_CONVERT,GstAudioConvert))
#define GST_IS_AUDIO_CONVERT(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_CONVERT))
#define GST_IS_AUDIO_CONVERT_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_CONVERT))

GST_DEBUG_CATEGORY_EXTERN (audio_convert_debug);
#define GST_CAT_DEFAULT (audio_convert_debug)

typedef struct _GstAudioConvert GstAudioConvert;
typedef struct _GstAudioConvertCaps GstAudioConvertCaps;
typedef struct _GstAudioConvertClass GstAudioConvertClass;

/* this struct is a handy way of passing around all the caps info ... */
struct _GstAudioConvertCaps
{
  /* general caps */
  gboolean is_int;
  gint endianness;
  gint width;
  gint rate;
  gint channels;
  GstAudioChannelPosition *pos;

  /* int audio caps */
  gboolean sign;
  gint depth;

  /* float audio caps */
  gint buffer_frames;
};

struct _GstAudioConvert
{
  GstBaseTransform element;

  GstAudioConvertCaps srccaps;
  GstAudioConvertCaps sinkcaps;

  GstCaps *src_prefered;
  GstCaps *sink_prefered;

  /* channel conversion matrix, m[in_channels][out_channels].
   * If identity matrix, passthrough applies. */
  gfloat **matrix;

  /* conversion functions */
  GstBuffer *(*convert_internal) (GstAudioConvert * this, GstBuffer * buf);
};

struct _GstAudioConvertClass
{
  GstBaseTransformClass parent_class;
};

/*
 * Delete channel mixer matrix.
 */
void		gst_audio_convert_unset_matrix	(GstAudioConvert * this);

/*
 * Setup channel mixer matrix.
 */
void		gst_audio_convert_setup_matrix	(GstAudioConvert * this);

/*
 * Checks for passthrough (= identity matrix).
 */
gboolean	gst_audio_convert_passthrough	(GstAudioConvert * this);

/*
 * Do actual mixing.
 */
void		gst_audio_convert_mix		(GstAudioConvert * this,
						 gint32          * in_data,
						 gint32          * out_data,
						 gint              samples);

#endif /* __GST_CHANNEL_MIX_H__ */
