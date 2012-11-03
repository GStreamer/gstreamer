/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
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

#ifndef _GST_PATCHDETECT_H_
#define _GST_PATCHDETECT_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_PATCHDETECT   (gst_patchdetect_get_type())
#define GST_PATCHDETECT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PATCHDETECT,GstPatchdetect))
#define GST_PATCHDETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PATCHDETECT,GstPatchdetectClass))
#define GST_IS_PATCHDETECT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PATCHDETECT))
#define GST_IS_PATCHDETECT_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PATCHDETECT))

typedef struct _GstPatchdetect GstPatchdetect;
typedef struct _GstPatchdetectClass GstPatchdetectClass;

struct _GstPatchdetect
{
  GstBaseTransform base_patchdetect;

  GstVideoFormat format;
  int width;
  int height;

  int t;
  int valid;
  double by[10], bu[10], bv[10];
};

struct _GstPatchdetectClass
{
  GstBaseTransformClass base_patchdetect_class;
};

GType gst_patchdetect_get_type (void);

G_END_DECLS

#endif
