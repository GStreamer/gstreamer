/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * gstarts.h: Header for ARTS plugin
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


#ifndef __GST_ARTS_H__
#define __GST_ARTS_H__


#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_ARTS \
  (gst_arts_get_type())
#define GST_ARTS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ARTS,GstARTS))
#define GST_ARTS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ARTS,GstARTS))
#define GST_IS_ARTS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ARTS))
#define GST_IS_ARTS_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ARTS))

typedef struct _GstARTS GstARTS;
typedef struct _GstARTSClass GstARTSClass;

struct _GstARTS {
  GstElement element;

  GstPad *sinkpad, *srcpad;
  void *wrapper;
};

struct _GstARTSClass {
  GstElementClass parent_class;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_ARTS_H__ */
