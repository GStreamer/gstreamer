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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
	
extern GstElementDetails gst_spider_details;

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
	
  GList *     	factories; /* factories to use for plugging */		
};
	
struct _GstSpiderClass {
  GstBinClass parent_class;
};

/* default initialization stuff */
GType         	gst_spider_get_type             (void);

/* private functions to be called by GstSpiderIdentity */
void		gst_spider_plug   		(GstSpiderIdentity *ident);

	
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_SPIDER_H__ */
