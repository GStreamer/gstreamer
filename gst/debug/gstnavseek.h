/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_NAVSEEK_H__
#define __GST_NAVSEEK_H__


#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_NAVSEEK \
  (gst_navseek_get_type())
#define GST_NAVSEEK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NAVSEEK,GstNavSeek))
#define GST_NAVSEEK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NAVSEEK,GstNavSeekClass))
#define GST_IS_NAVSEEK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NAVSEEK))
#define GST_IS_NAVSEEK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NAVSEEK))

typedef struct _GstNavSeek GstNavSeek;
typedef struct _GstNavSeekClass GstNavSeekClass;

struct _GstNavSeek {
  GstElement element;
  GstPad *sinkpad;
  GstPad *srcpad;

  gdouble seek_offset;
};

struct _GstNavSeekClass {
  GstElementClass parent_class;
};

G_END_DECLS

#endif /* __GST_NAVSEEK_H__ */

