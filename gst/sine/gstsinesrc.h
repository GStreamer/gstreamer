/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstsinesrc.h: 
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


#ifndef __GST_SINESRC_H__
#define __GST_SINESRC_H__


#include <gst/gst.h>

G_BEGIN_DECLS


GstElementDetails gst_sinesrc_details;


#define GST_TYPE_SINESRC \
  (gst_sinesrc_get_type())
#define GST_SINESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SINESRC,GstSineSrc))
#define GST_SINESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SINESRC,GstSineSrcClass))
#define GST_IS_SINESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SINESRC))
#define GST_IS_SINESRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SINESRC))

typedef struct _GstSineSrc GstSineSrc;
typedef struct _GstSineSrcClass GstSineSrcClass;

struct _GstSineSrc {
  GstElement element;

  /* pads */
  GstPad *srcpad;
  GstDParamManager *dpman;

  /* parameters */
  gdouble volume;
  gdouble freq;
  
  /* lookup table data */
  gdouble *table_data;
  gdouble table_pos;
  gdouble table_inc;
  gint table_size;
  gdouble table_interp;
  gint table_lookup;
  gint table_lookup_next;
    
  /* audio parameters */
  gint samplerate;

  gint samples_per_buffer;
  gulong seq;
  
  guint64 timestamp;
  guint64 offset;

  gdouble accumulator;

  gboolean tags_pushed;
};

struct _GstSineSrcClass {
  GstElementClass parent_class;
};

GType gst_sinesrc_get_type(void);
gboolean gst_sinesrc_factory_init (GstElementFactory *factory);

G_END_DECLS


#endif /* __GST_SINESRC_H__ */
