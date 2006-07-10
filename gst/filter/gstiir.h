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
 */

#ifndef __GST_IIR_H__
#define __GST_IIR_H__

#include "gstfilter.h"
#include "iir.h"
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_IIR \
  (gst_iir_get_type())
#define GST_IIR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IIR,GstIIR))
#define GST_IIR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IIR,GstIIRClass))
#define GST_IS_IIR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IIR))
#define GST_IS_IIR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IIR))

typedef struct _GstIIR GstIIR;
typedef struct _GstIIRClass GstIIRClass;

/**
 * GstIIR:
 *
 * Opaque data structure.
 */
struct _GstIIR {
  GstBaseTransform element;
  
  double A, B;
  double gain;
  int stages;
  IIR_state *state;
};

struct _GstIIRClass {
  GstBaseTransformClass parent_class;
};

G_END_DECLS

#endif /* __GST_IIR_H__ */
