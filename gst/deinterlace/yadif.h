/*
 * GStreamer
 * Copyright (C) 2019 Jan Schmidt <jan@centricular.com>
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

#ifndef __YADIF_H__
#define __YADIF_H__

#define GST_TYPE_DEINTERLACE_YADIF (gst_deinterlace_method_yadif_get_type ())

GType gst_deinterlace_method_yadif_get_type (void);

void
gst_yadif_filter_line_mode0_sse2 (void *dst, const void *tzero, const void *bzero,
    const void *mone, const void *mp, const void *ttwo, const void *btwo, const void *tptwo, const void *bptwo,
    const void *ttone, const void *ttp, const void *bbone, const void *bbp, int w);

void
gst_yadif_filter_line_mode2_sse2 (void *dst, const void *tzero, const void *bzero,
    const void *mone, const void *mp, const void *ttwo, const void *btwo, const void *tptwo, const void *bptwo,
    const void *ttone, const void *ttp, const void *bbone, const void *bbp, int w);

void
gst_yadif_filter_line_mode0_ssse3 (void *dst, const void *tzero, const void *bzero,
    const void *mone, const void *mp, const void *ttwo, const void *btwo, const void *tptwo, const void *bptwo,
    const void *ttone, const void *ttp, const void *bbone, const void *bbp, int w);

void
gst_yadif_filter_line_mode2_ssse3 (void *dst, const void *tzero, const void *bzero,
    const void *mone, const void *mp, const void *ttwo, const void *btwo, const void *tptwo, const void *bptwo,
    const void *ttone, const void *ttp, const void *bbone, const void *bbp, int w);

#endif
