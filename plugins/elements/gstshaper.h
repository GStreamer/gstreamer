/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstshaper.h: 
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


#ifndef __GST_SHAPER_H__
#define __GST_SHAPER_H__


#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_SHAPER \
  (gst_shaper_get_type())
#define GST_SHAPER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHAPER,GstShaper))
#define GST_SHAPER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHAPER,GstShaperClass))
#define GST_IS_SHAPER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHAPER))
#define GST_IS_SHAPER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHAPER))
    typedef enum
{
  SHAPER_POLICY_TIMESTAMPS = 1,
  SHAPER_POLICY_BUFFERSIZE
} GstShaperPolicyType;

typedef struct _GstShaper GstShaper;
typedef struct _GstShaperClass GstShaperClass;

struct _GstShaper
{
  GstElement element;

  GSList *connections;
  gint nconnections;

  GstShaperPolicyType policy;

  gboolean silent;
  gchar *last_message;
};

struct _GstShaperClass
{
  GstElementClass parent_class;
};

GType gst_shaper_get_type (void);
gboolean gst_shaper_factory_init (GstElementFactory * factory);


G_END_DECLS
#endif /* __GST_SHAPER_H__ */
