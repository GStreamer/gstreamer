/* GStreamer trm plugin
 * Copyright (C) 2004 Jeremy Simon <jsimon13@yahoo.fr>
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


#ifndef __GST_TRM_H__
#define __GST_TRM_H__

#include <gst/gst.h>
#include <gst/gsttaglist.h>
#include <gst/tag/tag.h>

#include <musicbrainz/mb_c.h>

G_BEGIN_DECLS

#define GST_TYPE_TRM            (gst_trm_get_type())
#define GST_TRM(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRM,GstTRM))
#define GST_TRM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRM,GstTRMClass))
#define GST_IS_TRM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRM))
#define GST_IS_TRM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRM))

typedef struct _GstTRM GstTRM;
typedef struct _GstTRMClass GstTRMClass;

/**
 *  GstTRM
 *
 *  GStreamer TRM element. This structure is opaque (private).
 *
 **/

struct _GstTRM {
  GstElement element;

  /*< private >*/
  GstPad   *sinkpad;
  GstPad   *srcpad;

  trm_t     trm;

  gchar    *proxy_address;
  guint     proxy_port;

  gint      depth;  
  gint      rate;
  gint      channels;

  gboolean  data_available;
  gboolean  signature_available;
};

struct _GstTRMClass {
  GstElementClass parent_class;
};

GType gst_trm_get_type(void);

G_END_DECLS

#endif /* __GST_TRM_H__ */
