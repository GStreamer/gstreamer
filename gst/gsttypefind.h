/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsttypefind.h: 
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


#ifndef __GST_TYPE_FIND_H__
#define __GST_TYPE_FIND_H__

#ifndef GST_DISABLE_TYPE_FIND

#include <gst/gstelement.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

GstElementDetails gst_type_find_details;

#define GST_TYPE_TYPE_FIND \
  (gst_type_find_get_type())
#define GST_TYPE_FIND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TYPE_FIND,GstTypeFind))
#define GST_TYPE_FIND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TYPE_FIND,GstTypeFindClass))
#define GST_IS_TYPE_FIND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TYPE_FIND))
#define GST_IS_TYPE_FIND_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TYPE_FIND))

typedef struct _GstTypeFind 		GstTypeFind;
typedef struct _GstTypeFindClass 	GstTypeFindClass;

struct _GstTypeFind {
  GstElement element;

  GstPad *sinkpad;

  GstCaps *caps;
};

struct _GstTypeFindClass {
  GstElementClass parent_class;

  /* signals */
  void (*have_type) (GstElement *element);
};

GType gst_type_find_get_type (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* GST_DISABLE_TYPE_FIND */

#endif /* __GST_TYPE_FIND_H__ */
