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


#ifndef __GST_TYPEFIND_H__
#define __GST_TYPEFIND_H__

#ifndef GST_DISABLE_TYPEFIND

#include <gst/gstelement.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

GstElementDetails gst_typefind_details;

#define GST_TYPE_TYPEFIND \
  (gst_typefind_get_type())
#define GST_TYPEFIND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TYPEFIND,GstTypeFind))
#define GST_TYPEFIND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TYPEFIND,GstTypeFindClass))
#define GST_IS_TYPEFIND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TYPEFIND))
#define GST_IS_TYPEFIND_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TYPEFIND))

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

GType gst_typefind_get_type (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* GST_DISABLE_TYPEFIND */

#endif /* __GST_TYPEFIND_H__ */
