/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdplinearinterp.h: linear interpolation dynamic parameter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_DP_LININT_H__
#define __GST_DP_LININT_H__

#include <gst/gstobject.h>
#include "dparam.h"

G_BEGIN_DECLS
#define GST_TYPE_DP_LININT			(gst_dp_linint_get_type ())
#define GST_DP_LININT(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DP_LININT,GstDParamLinInterp))
#define GST_DP_LININT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DP_LININT,GstDParamLinInterp))
#define GST_IS_DP_LININT(obj)			(G_TYPE_CHECK_INSTANCE_TYPE	((obj), GST_TYPE_DP_LININT))
#define GST_IS_DP_LININT_CLASS(obj)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DP_LININT))
typedef struct _GstDParamLinInterp GstDParamLinInterp;
typedef struct _GstDParamLinInterpClass GstDParamLinInterpClass;

struct _GstDParamLinInterp
{
  GstDParam dparam;

};

struct _GstDParamLinInterpClass
{
  GstDParamClass parent_class;

  /* signal callbacks */
};

GType gst_dp_linint_get_type (void);
GstDParam *gst_dp_linint_new (GType type);

G_END_DECLS
#endif /* __GST_DP_LININT_H__ */
