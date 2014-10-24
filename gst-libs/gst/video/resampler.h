/* GStreamer
 * Copyright (C) <2014> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_RESAMPLER_H__
#define __GST_RESAMPLER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstResampler GstResampler;

/**
 * GstResamplerMethod:
 * @GST_RESAMPLER_METHOD_NEAREST: Duplicates the samples when
 *    upsampling and drops when downsampling
 * @GST_RESAMPLER_METHOD_LINEAR: Uses linear interpolation to reconstruct
 *    missing samples and averaging to downsample
 * @GST_RESAMPLER_METHOD_BICUBIC: Uses bicubic interpolation
 * @GST_RESAMPLER_METHOD_SINC: Uses sinc interpolation
 * @GST_RESAMPLER_METHOD_LANCZOS: Uses lanczos interpolation
 *
 * Different subsampling and upsampling methods
 *
 * Since: 1.6
 */
typedef enum {
  GST_RESAMPLER_METHOD_NEAREST,
  GST_RESAMPLER_METHOD_LINEAR,
  GST_RESAMPLER_METHOD_BICUBIC,
  GST_RESAMPLER_METHOD_SINC,
  GST_RESAMPLER_METHOD_LANCZOS,
} GstResamplerMethod;

/**
 * GstResamplerFlags:
 * @GST_RESAMPLER_FLAG_NONE: no flags
 *
 * Different resampler flags.
 *
 * Since: 1.6
 */
typedef enum {
  GST_RESAMPLER_FLAG_NONE                 = (0),
} GstResamplerFlags;

/**
 * GstResampler:
 * @in_size: the input size
 * @out_size: the output size
 * @max_taps: the maximum number of taps
 * @n_phases: the number of phases
 * @offset: array with the source offset for each output element
 * @phase: array with the phase to use for each output element
 * @n_taps: array with new number of taps for each phase
 * @taps: the taps for all phases
 *
 * A structure holding resampler information.
 *
 * Since: 1.6
 */
struct _GstResampler
{
  gint in_size;
  gint out_size;
  guint max_taps;
  guint n_phases;
  guint *offset;
  guint *phase;
  guint *n_taps;
  gdouble *taps;

  gpointer _gst_reserved[GST_PADDING];
};


gboolean    gst_resampler_init            (GstResampler *resampler,
                                           GstResamplerMethod method,
                                           GstResamplerFlags flags,
                                           guint n_phases, guint n_taps,
                                           gdouble shift,
                                           guint in_size, guint out_size,
                                           GstStructure *options);

void        gst_resampler_clear           (GstResampler *resampler);

G_END_DECLS

#endif /* __GST_RESAMPLER_H__ */
