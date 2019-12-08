/* Copyright (C) <2018, 2019> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018, 2019> Žan Doberšek <zdobersek@igalia.com>
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


#pragma once

#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

GType gst_wpe_audio_pad_get_type(void);
#define GST_TYPE_WPE_AUDIO_PAD            (gst_wpe_audio_pad_get_type())
#define GST_WPE_AUDIO_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WPE_AUDIO_PAD,GstWpeAudioPad))
#define GST_IS_WPE_AUDIO_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WPE_AUDIO_PAD))
#define GST_WPE_AUDIO_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WPE_AUDIO_PAD,GstWpeAudioPadClass))
#define GST_IS_WPE_AUDIO_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WPE_AUDIO_PAD))
#define GST_WPE_AUDIO_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WPE_AUDIO_PAD,GstWpeAudioPadClass))

typedef struct _GstWpeAudioPad GstWpeAudioPad;
typedef struct _GstWpeAudioPadClass GstWpeAudioPadClass;

struct _GstWpeAudioPad
{
  GstGhostPad      parent;

  GstAudioInfo     info;
  GstClockTime     buffer_time;
  gboolean         discont_pending;
};

struct _GstWpeAudioPadClass
{
  GstGhostPadClass parent_class;
};


#define GST_TYPE_WPE_SRC            (gst_wpe_src_get_type())
#define GST_WPE_SRC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WPE_SRC,GstWpeSrc))
#define GST_WPE_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_WPE_SRC,GstWpeSrcClass))
#define GST_IS_WPE_SRC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WPE_SRC))
#define GST_IS_WPE_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_WPE_SRC))

typedef struct _GstWpeSrc GstWpeSrc;
typedef struct _GstWpeSrcClass GstWpeSrcClass;

struct _GstWpeSrcClass
{
  GstBinClass parent_class;
};

GType gst_wpe_src_get_type (void);

G_END_DECLS
