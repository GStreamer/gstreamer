/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wtay@chello.be>
 *
 * gstspider.h: Header for GstSpider object
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
 
#ifndef __GST_SPIDER_H__
#define __GST_SPIDER_H__

#include <gst/gst.h>
#include "gstspideridentity.h"

G_BEGIN_DECLS
	
extern GstElementDetails gst_spider_details;
GST_DEBUG_CATEGORY_EXTERN(gst_spider_debug);

/*
 * Theory of operation:
 * When connecting a sink to a source, GstSpiderConnections are used to keep track
 * of the current status of the link. sink -> src is the path we intend to
 * plug. current is how far we've come. If current equals
 * - NULL, there is no possible path, 
 * - src, the link is established.
 * - sink, it wasn't tried to establish a link.
 * - something else, we have come that far while plugging.
 * signal_id is used to remember the signal_id when we are waiting for a "new_pad"
 * callback during link.
 * When a path is established, the elements in the path (excluding sink and src)
 * are refcounted once for every path.
 * A GstSpider keeps a list of all GstSpiderConnections in it.
 */
typedef struct {
  GstSpiderIdentity *src;
  /* dunno if the path should stay here or if its too much load.
   * it's at least easier then always searching it */
  GList *path;
  GstElement *current;
  gulong signal_id;
} GstSpiderConnection;

#define GST_TYPE_SPIDER \
  (gst_spider_get_type())
#define GST_SPIDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPIDER,GstSpider))
#define GST_SPIDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPIDER,GstSpiderClass)) 
#define GST_IS_SPIDER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPIDER))
#define GST_IS_SPIDER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPIDER))
	
typedef struct _GstSpider GstSpider;
typedef struct _GstSpiderClass GstSpiderClass;

struct _GstSpider {
  GstBin        parent;
	
  GstSpiderIdentity *sink_ident;
  GList *     	factories; /* factories to use for plugging */

  GList *	links; /* GStSpiderConnection list of all links */
};
	
struct _GstSpiderClass {
  GstBinClass parent_class;
};

/* default initialization stuff */
GType         	gst_spider_get_type             (void);

/* private link functions to be called by GstSpiderIdentity */
void		gst_spider_identity_plug	(GstSpiderIdentity *ident);
void		gst_spider_identity_unplug	(GstSpiderIdentity *ident);

G_END_DECLS

#endif /* __GST_SPIDER_H__ */
