/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosselement.h: 
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

#ifndef __GST_OSS_ELEMENT_H__
#define __GST_OSS_ELEMENT_H__

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (oss_debug);
#define GST_CAT_DEFAULT oss_debug

G_BEGIN_DECLS

#define GST_TYPE_OSSELEMENT \
  (gst_osselement_get_type())
#define GST_OSSELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSSELEMENT,GstOssElement))
#define GST_OSSELEMENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSSELEMENT,GstOssElementClass))
#define GST_IS_OSSELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSSELEMENT))
#define GST_IS_OSSELEMENT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSSELEMENT))
#define GST_OSSELEMENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_OSSELEMENT, GstOssElementClass))

typedef struct _GstOssElement GstOssElement;
typedef struct _GstOssElementClass GstOssElementClass;

typedef enum {
  GST_OSSELEMENT_READ,
  GST_OSSELEMENT_WRITE,
} GstOssOpenMode;

typedef struct _GstOssDeviceCombination {
  gchar *dsp, *mixer;
} GstOssDeviceCombination;

struct _GstOssElement
{
  /* yes, we're a gstelement too */
  GstElement     parent;

  gchar		*device,
		*mixer_dev;

  /* device state */
  int		 fd;
  int		 caps; /* the capabilities */
  gint		 format;
  gint		 fragment;
  guint64	 fragment_time;
  gint		 fragment_size;
  GstOssOpenMode mode;

  /* stats bytes per *second* */
  guint		 bps;

  /* parameters */
  gint 		 law;
  gint 		 endianness;
  gboolean	 sign;
  gint		 width;
  gint		 depth;
  gint		 channels;
  gint		 rate;

  /* mixer stuff */
  GList		*tracklist;
  guint32	 stereomask,
		 recdevs,
		 recmask,
		 mixcaps;
  gint		 mixer_fd;
  gchar		*device_name;
};

struct _GstOssElementClass {
  GstElementClass klass;

  GList		*device_combinations;
};

GType		gst_osselement_get_type		(void);

/* some useful functions */
gboolean 	gst_osselement_parse_caps 	(GstOssElement *oss,
						 const GstCaps      *caps);
gboolean	gst_osselement_merge_fixed_caps (GstOssElement *oss,
						 GstCaps      *caps);
	
gboolean 	gst_osselement_sync_parms 	(GstOssElement *oss);
void		gst_osselement_reset 		(GstOssElement *oss);

gboolean 	gst_osselement_convert 	 	(GstOssElement *oss, 
						 GstFormat      src_format,
						 gint64         src_value,
		                       		 GstFormat     *dest_format,
						 gint64        *dest_value);

G_END_DECLS

#endif /* __GST_OSS_ELEMENT_H__ */
