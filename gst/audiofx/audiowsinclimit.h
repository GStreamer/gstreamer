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
 */

#ifndef __GST_LPWSINC_H__
#define __GST_LPWSINC_H__

#include "gstfilter.h"
#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_LPWSINC \
  (gst_lpwsinc_get_type())
#define GST_LPWSINC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LPWSINC,GstLPWSinc))
#define GST_LPWSINC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LPWSINC,GstLPWSincClass))
#define GST_IS_LPWSINC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LPWSINC))
#define GST_IS_LPWSINC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LPWSINC))

typedef struct _GstLPWSinc GstLPWSinc;
typedef struct _GstLPWSincClass GstLPWSincClass;

typedef void (*GstLPWSincProcessFunc) (GstLPWSinc *, guint8 *, guint8 *, guint);

/**
 * GstLPWSinc:
 *
 * Opaque data structure.
 */
struct _GstLPWSinc {
  GstAudioFilter element;

  /* < private > */
  GstLPWSincProcessFunc process;

  gint mode;
  gint window;
  gdouble frequency;
  gint kernel_length;           /* length of the filter kernel */

  gdouble *residue;             /* buffer for left-over samples from previous buffer */
  gdouble *kernel;              /* filter kernel */
  gboolean have_kernel;
};

struct _GstLPWSincClass {
  GstAudioFilterClass parent_class;
};

GType gst_lpwsinc_get_type (void);

G_END_DECLS

#endif /* __GST_LPWSINC_H__ */
