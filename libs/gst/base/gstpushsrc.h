/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstpushsrc.h: 
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

#ifndef __GST_PUSH_SRC_H__
#define __GST_PUSH_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_PUSH_SRC  		(gst_push_src_get_type())
#define GST_PUSH_SRC(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PUSH_SRC,GstPushSrc))
#define GST_PUSH_SRC_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PUSH_SRC,GstPushSrcClass))
#define GST_PUSH_SRC_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PUSH_SRC, GstPushSrcClass))
#define GST_IS_PUSH_SRC(obj)  		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PUSH_SRC))
#define GST_IS_PUSH_SRC_CLASS(obj)  	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PUSH_SRC))

/* base class for block based sources
 *
 * This class is mostly usefull for elements that cannot do
 * random access, or at least very slowly. The source usually
 * prefers to push out a fixed size buffer.
 *
 * Classes extending this base class will usually be scheduled
 * in a push based mode. It the peer accepts to operate without
 * offsets and withing the limits of the allowed block size, this
 * class can operate in getrange based mode automatically.
 *
 * The subclass should extend the methods from the baseclass in
 * addition to the create method.
 *
 * Seeking, flushing, scheduling and sync is all handled by this
 * base class.
 */
typedef struct _GstPushSrc GstPushSrc;
typedef struct _GstPushSrcClass GstPushSrcClass;

struct _GstPushSrc {
  GstBaseSrc     parent;
};

struct _GstPushSrcClass {
  GstBaseSrcClass parent_class;

  /* ask the subclass to create a buffer */
  GstFlowReturn (*create) (GstPushSrc *src, GstBuffer **buf);
};

GType gst_push_src_get_type(void);

G_END_DECLS

#endif /* __GST_PUSH_SRC_H__ */
