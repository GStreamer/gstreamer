/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wtay@chello.be>
 *
 * gstspideridentity.h: 
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


#ifndef __GST_SPIDER_IDENTITY_H__
#define __GST_SPIDER_IDENTITY_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SPIDER_IDENTITY \
  (gst_spider_identity_get_type())
#define GST_SPIDER_IDENTITY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPIDER_IDENTITY,GstSpiderIdentity))
#define GST_SPIDER_IDENTITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPIDER_IDENTITY,GstSpiderIdentityClass))
#define GST_IS_SPIDER_IDENTITY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPIDER_IDENTITY))
#define GST_IS_SPIDER_IDENTITY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPIDER_IDENTITY))

typedef struct _GstSpiderIdentity GstSpiderIdentity;
typedef struct _GstSpiderIdentityClass GstSpiderIdentityClass;

struct _GstSpiderIdentity {
  GstElement element;

  /* sink and source */
  GstPad *sink;
  GstPad *src;
	
  /* plugged into autoplugger yet? */
  gboolean plugged;
	
  /* Caps from typefinding */
  GstCaps *caps;
};

struct _GstSpiderIdentityClass {
  GstElementClass parent_class;

};

GType 				gst_spider_identity_get_type			(void);

GstSpiderIdentity* 		gst_spider_identity_new_sink			(gchar *name);
GstSpiderIdentity* 		gst_spider_identity_new_src			(gchar *name);
GstPad*				gst_spider_identity_request_new_pad		(GstElement *element, GstPadTemplate *templ, const gchar *name);

G_END_DECLS

#endif /* __GST_SPIDER_IDENTITY_H__ */
