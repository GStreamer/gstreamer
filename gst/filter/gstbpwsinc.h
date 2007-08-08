/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
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
 * 
 * 
 * this windowed sinc filter is taken from the freely downloadable DSP book,
 * "The Scientist and Engineer's Guide to Digital Signal Processing",
 * chapter 16
 * available at http://www.dspguide.com/
 *
 * FIXME:
 * - this filter is totally unoptimized !
 * - we do not destroy the allocated memory for filters and residue
 * - this might be improved upon with bytestream
 */

#ifndef __GST_BPWSINC_H__
#define __GST_BPWSINC_H__

#include "gstfilter.h"
#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_BPWSINC \
  (gst_bpwsinc_get_type())
#define GST_BPWSINC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BPWSINC,GstBPWSinc))
#define GST_BPWSINC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BPWSINC,GstBPWSincClass))
#define GST_IS_BPWSINC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BPWSINC))
#define GST_IS_BPWSINC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BPWSINC))

typedef struct _GstBPWSinc GstBPWSinc;
typedef struct _GstBPWSincClass GstBPWSincClass;

/**
 * GstBPWSinc:
 *
 * Opaque data structure.
 */
struct _GstBPWSinc {
  GstAudioFilter element;

  double frequency;
  double lower_frequency, upper_frequency;
  int wing_size;                /* length of a "wing" of the filter; 
                                   actual length is 2 * wing_size + 1 */

  gfloat *residue;              /* buffer for left-over samples from previous buffer */
  double *kernel;
};

struct _GstBPWSincClass {
  GstAudioFilterClass parent_class;
};

G_END_DECLS

#endif /* __GST_BPWSINC_H__ */
