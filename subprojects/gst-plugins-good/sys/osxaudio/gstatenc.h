/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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

#ifndef _GST_ATENC_H_
#define _GST_ATENC_H_

#include <AudioToolbox/AudioToolbox.h>
#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>
#include <gst/pbutils/codec-utils.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstqueuearray.h>

#include "gstosxcoreaudiocommon.h"

G_BEGIN_DECLS
#define GST_TYPE_ATENC   (gst_atenc_get_type())
#define GST_ATENC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ATENC,GstATEnc))
#define GST_ATENC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ATENC,GstATEncClass))
#define GST_IS_ATENC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ATENC))
#define GST_IS_ATENC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ATENC))
typedef struct _GstATEnc GstATEnc;
typedef struct _GstATEncClass GstATEncClass;

/**
 * GstATEncRateControl:
 * @GST_ATENC_RATE_CONTROL_CONSTANT: Constant bitrate
 * @GST_ATENC_RATE_CONTROL_LONG_TERM_AVERAGE: Long-term-average bitrate
 * @GST_ATENC_RATE_CONTROL_VARIABLE_CONSTRAINED: Variable constrained bitrate
 * @GST_ATENC_RATE_CONTROL_VARIABLE: Variable bitrate
 *
 * Since: 1.26
 */
typedef enum
{
  GST_ATENC_RATE_CONTROL_CONSTANT = 0,
  GST_ATENC_RATE_CONTROL_LONG_TERM_AVERAGE = 1,
  GST_ATENC_RATE_CONTROL_VARIABLE_CONSTRAINED = 2,
  GST_ATENC_RATE_CONTROL_VARIABLE = 3,
} GstATEncRateControl;

typedef struct
{
  gint channels;
  AudioChannelLayoutTag aac_tag;
  GstAudioChannelPosition positions[8];
} GstATEncLayout;

struct _GstATEnc
{
  GstAudioEncoder encoder;
  AudioConverterRef converter;
  UInt32 max_output_buffer_size;
  UInt32 n_output_samples;
  GstVecDeque *input_queue;
  GstAudioBuffer *used_buffer;
  gboolean input_eos;

  GstATEncRateControl rate_control;
  guint32 bitrate;
  guint32 vbr_quality;
};

struct _GstATEncClass
{
  GstAudioEncoderClass encoder_class;
};

GType gst_atenc_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (atenc);

G_END_DECLS
#endif
