/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstautoplug.h: Header for autoplugging functionality
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


#ifndef __GST_STATIC_AUTOPLUG_H__
#define __GST_STATIC_AUTOPLUG_H__

#include <gst/gstautoplug.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_STATIC_AUTOPLUG \
  (gst_static_autoplug_get_type())
#define GST_STATIC_AUTOPLUG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STATIC_AUTOPLUG,GstStaticAutoplug))
#define GST_STATIC_AUTOPLUG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STATIC_AUTOPLUG,GstStaticAutoplugClass))
#define GST_IS_STATIC_AUTOPLUG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STATIC_AUTOPLUG))
#define GST_IS_STATIC_AUTOPLUG_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STATIC_AUTOPLUG))

typedef struct _GstStaticAutoplug GstStaticAutoplug;
typedef struct _GstStaticAutoplugClass GstStaticAutoplugClass;

struct _GstStaticAutoplug {
  GstAutoplug object;
};

struct _GstStaticAutoplugClass {
  GstAutoplugClass parent_class;
};


GType			gst_static_autoplug_get_type		(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_STATIC_AUTOPLUG_H__ */

