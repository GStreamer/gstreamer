/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_AVI_H__
#define __GST_AVI_H__

#include <gst/gst.h>

typedef struct _gst_riff_avih {
  guint32 us_frame;          /* microsec per frame */
  guint32 max_bps;           /* byte/s overall */
  guint32 pad_gran;          /* pad_granularity */
  guint32 flags;
/* flags values */
#define GST_RIFF_AVIH_HASINDEX       0x00000010 /* has idx1 chunk */
#define GST_RIFF_AVIH_MUSTUSEINDEX   0x00000020 /* must use idx1 chunk to determine order */
#define GST_RIFF_AVIH_ISINTERLEAVED  0x00000100 /* AVI file is interleaved */
#define GST_RIFF_AVIH_WASCAPTUREFILE 0x00010000 /* specially allocated used for capturing real time video */
#define GST_RIFF_AVIH_COPYRIGHTED    0x00020000 /* contains copyrighted data */
  guint32 tot_frames;        /* # of frames (all) */
  guint32 init_frames;       /* initial frames (???) */
  guint32 streams;
  guint32 bufsize;           /* suggested buffer size */
  guint32 width;
  guint32 height;
  guint32 scale;
  guint32 rate;
  guint32 start;
  guint32 length;
} gst_riff_avih;

#endif /* __GST_AVI_H__ */
