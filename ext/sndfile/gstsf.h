/* GStreamer libsndfile plugin
 * Copyright (C) 2003 Andy Wingo <wingo at pobox dot com>
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


#ifndef __GST_SFSINK_H__
#define __GST_SFSINK_H__


#include <gst/gst.h>
#include <sndfile.h>


G_BEGIN_DECLS


#define GST_TYPE_SF \
  (gst_sf_get_type())
#define GST_SF(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SF,GstSF))
#define GST_SF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SF,GstSFClass))
#define GST_IS_SF(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SF))
#define GST_IS_SF_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SF))

#define GST_TYPE_SFSRC \
  (gst_sfsrc_get_type())
#define GST_SFSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SFSRC,GstSF))
#define GST_SFSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SFSRC,GstSFClass))
#define GST_IS_SFSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SFSRC))
#define GST_IS_SFSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SFSRC))

#define GST_TYPE_SFSINK \
  (gst_sfsink_get_type())
#define GST_SFSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SFSINK,GstSF))
#define GST_SFSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SFSINK,GstSFClass))
#define GST_IS_SFSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SFSINK))
#define GST_IS_SFSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SFSINK))

typedef struct _GstSF GstSF;
typedef struct _GstSFClass GstSFClass;

typedef enum {
  GST_SF_OPEN		= GST_ELEMENT_FLAG_LAST,
  GST_SF_FLAG_LAST 	= GST_ELEMENT_FLAG_LAST + 2,
} GstSFlags;

typedef struct {
  GstPad *pad;
  gint num;
  gboolean caps_set;
} GstSFChannel;

#define GST_SF_CHANNEL(l) ((GstSFChannel*)l->data)

struct _GstSF {
  GstElement element;
  GList *channels;

  GstClock *clock, *provided_clock;

  gchar *filename;
  SNDFILE *file;
  void *buffer;

  gboolean loop;
  gboolean create_pads;
  gint channelcount;
  gint numchannels;
  gint format_major;
  gint format_subtype;
  gint format;

  gint rate;
  gint buffer_frames;

  guint64 time;
};

struct _GstSFClass {
  GstElementClass parent_class;
};

GType	gst_sf_get_type		(void);
GType	gst_sfsrc_get_type	(void);
GType	gst_sfsink_get_type	(void);


G_END_DECLS


#endif /* __GST_SFSINK_H__ */
