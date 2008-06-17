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

/*
 *
 *   This is a temporary copy of GstBaseSrc/GstPushSrc for the resin
 *   DVD components, to work around a deadlock with source elements that
 *   send seeks to themselves. 
 *
 */
#ifndef __GST_PUSH_SRC_H__
#define __GST_PUSH_SRC_H__

#include <gst/gst.h>
#include "rsnbasesrc.h"

G_BEGIN_DECLS

#define RSN_TYPE_PUSH_SRC  		(rsn_push_src_get_type())
#define GST_PUSH_SRC(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),RSN_TYPE_PUSH_SRC,RsnPushSrc))
#define GST_PUSH_SRC_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),RSN_TYPE_PUSH_SRC,RsnPushSrcClass))
#define GST_PUSH_SRC_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), RSN_TYPE_PUSH_SRC, RsnPushSrcClass))
#define GST_IS_PUSH_SRC(obj)  		(G_TYPE_CHECK_INSTANCE_TYPE((obj),RSN_TYPE_PUSH_SRC))
#define GST_IS_PUSH_SRC_CLASS(klass)  	(G_TYPE_CHECK_CLASS_TYPE((klass),RSN_TYPE_PUSH_SRC))

typedef struct _RsnPushSrc RsnPushSrc;
typedef struct _RsnPushSrcClass RsnPushSrcClass;

/**
 * RsnPushSrc:
 * @parent: the parent base source object.
 *
 * The opaque #RsnPushSrc data structure.
 */
struct _RsnPushSrc {
  RsnBaseSrc     parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _RsnPushSrcClass {
  RsnBaseSrcClass parent_class;

  /* ask the subclass to create a buffer */
  GstFlowReturn (*create) (RsnPushSrc *src, GstBuffer **buf);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType rsn_push_src_get_type(void);

G_END_DECLS

#endif /* __GST_PUSH_SRC_H__ */
