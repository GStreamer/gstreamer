/*
 * GStreamer
 * Copyright (C) 2008 Rov Juvano <rovjuvano@users.sourceforge.net>
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

#ifndef __GST_SCALETEMPO_H__
#define __GST_SCALETEMPO_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS
#define GST_TYPE_SCALETEMPO            (gst_scaletempo_get_type())
#define GST_SCALETEMPO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SCALETEMPO, GstScaletempo))
#define GST_SCALETEMPO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_TYPE_SCALETEMPO, GstScaletempoClass))
#define GST_IS_SCALETEMPO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SCALETEMPO))
#define GST_IS_SCALETEMPO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_TYPE_SCALETEMPO))
typedef struct _GstScaletempo GstScaletempo;
typedef struct _GstScaletempoClass GstScaletempoClass;

struct _GstScaletempo
{
  GstBaseTransform element;
};

struct _GstScaletempoClass
{
  GstBaseTransformClass parent_class;
};

GType gst_scaletempo_get_type (void);

G_END_DECLS
#endif /* __GST_SCALETEMPO_H__ */
