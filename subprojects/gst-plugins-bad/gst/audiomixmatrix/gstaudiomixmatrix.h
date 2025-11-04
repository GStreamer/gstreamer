/*
 * GStreamer
 * Copyright (C) 2017 Vivia Nikolaidou <vivia@toolsonair.com>
 *
 * gstaudiomixmatrix.h
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

#ifndef __GST_AUDIO_MIX_MATRIX_H__
#define __GST_AUDIO_MIX_MATRIX_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

#define GST_TYPE_AUDIO_MIX_MATRIX (gst_audio_mix_matrix_get_type())
G_DECLARE_FINAL_TYPE (GstAudioMixMatrix, gst_audio_mix_matrix,
    GST, AUDIO_MIX_MATRIX, GstBaseTransform)

typedef enum _GstAudioMixMatrixMode
{
  GST_AUDIO_MIX_MATRIX_MODE_MANUAL = 0,
  GST_AUDIO_MIX_MATRIX_MODE_FIRST_CHANNELS = 1
} GstAudioMixMatrixMode;

GST_ELEMENT_REGISTER_DECLARE (audiomixmatrix);

G_END_DECLS
#endif /* __GST_AUDIO_MIX_MATRIX_H__ */
