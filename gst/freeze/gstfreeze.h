/* gst-freeze -- Source freezer
 * Copyright (C) 2005 Gergely Nagy <gergely.nagy@neteyes.hu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#ifndef __GST_FREEZE_H__
#define __GST_FREEZE_H__ 1

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_FREEZE (gst_freeze_get_type ())
#define GST_FREEZE(obj)							\
  (G_TYPE_CHECK_INSTANCE_CAST (obj, GST_TYPE_FREEZE, GstFreeze))
#define GST_FREEZE_CLASS(klass)					\
  (G_TYPE_CHECK_CLASS_CAST (klass, GST_TYPE_FREEZE, GstFreezeClass))
#define GST_IS_FREEZE(obj)				\
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, GST_TYPE_FREEZE))
#define GST_IS_FREEZE_CLASS(klass)			\
  (G_TYPE_CHECK_CLASS_TYPE (klass, GST_TYPE_FREEZE))
typedef struct _GstFreeze GstFreeze;
typedef struct _GstFreezeClass GstFreezeClass;

struct _GstFreeze
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GQueue *buffers;
  GstBuffer *current;
  
  guint max_buffers;

  gint64 timestamp_offset;
  gint64 offset;
  GstClockTime running_time;
  gboolean on_flush;
};

struct _GstFreezeClass
{
  GstElementClass parent_class;
};

GType gst_freeze_get_type (void);

G_END_DECLS
#endif
/*
 * Local variables:
 * mode: c
 * file-style: k&r
 * c-basic-offset: 2
 * arch-tag: 559a2214-86a1-4c2f-b497-bdcc5f82acf1
 * End:
 */
