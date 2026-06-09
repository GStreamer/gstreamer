/* GStreamer
 * Copyright (C) 2026 Felix-Gong <gongxiaofei24@iscas.ac.cn>
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

#ifndef AUDIO_RESAMPLER_RVV_H
#define AUDIO_RESAMPLER_RVV_H

#include "audio-resampler-macros.h"

DECL_RESAMPLE_FUNC (gint16, full, 1, rvv);
DECL_RESAMPLE_FUNC (gint16, linear, 1, rvv);
DECL_RESAMPLE_FUNC (gint16, cubic, 1, rvv);

DECL_RESAMPLE_FUNC (gint32, full, 1, rvv);
DECL_RESAMPLE_FUNC (gint32, linear, 1, rvv);
DECL_RESAMPLE_FUNC (gint32, cubic, 1, rvv);

DECL_RESAMPLE_FUNC (gfloat, full, 1, rvv);
DECL_RESAMPLE_FUNC (gfloat, linear, 1, rvv);
DECL_RESAMPLE_FUNC (gfloat, cubic, 1, rvv);

void interpolate_gint16_linear_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride);
void interpolate_gint16_cubic_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride);

void interpolate_gint32_linear_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride);
void interpolate_gint32_cubic_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride);

void interpolate_gfloat_linear_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride);
void interpolate_gfloat_cubic_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride);

#endif /* AUDIO_RESAMPLER_RVV_H */
