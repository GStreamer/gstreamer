/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosssink.c: 
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

#ifndef __GST_OSSCOMMON_H__
#define __GST_OSSCOMMON_H__

#include <gst/gst.h>

typedef struct _GstOssCommon	GstOssCommon;

typedef enum {
  GST_OSSCOMMON_READ,
  GST_OSSCOMMON_WRITE,
} GstOssOpenMode;

struct _GstOssCommon
{
  gchar		*device;
  /* device state */
  int		 fd;
  int		 caps; /* the capabilities */
  gint		 format;
  gint		 fragment;
  guint64	 fragment_time;
  gint		 fragment_size;
  GstOssOpenMode mode;
  /* stats */
  guint		 bps;

  /* parameters */
  gint 		 law;
  gint 		 endianness;
  gboolean	 sign;
  gint		 width;
  gint		 depth;
  gint		 channels;
  gint		 rate;
};

void       	gst_osscommon_init 		(GstOssCommon *common);
void       	gst_osscommon_reset 		(GstOssCommon *common);

gboolean 	gst_osscommon_open_audio 	(GstOssCommon *common, 
		                                 GstOssOpenMode mode, gchar **error);
void	 	gst_osscommon_close_audio 	(GstOssCommon *common);

gboolean 	gst_osscommon_parse_caps 	(GstOssCommon *common, GstCaps *caps);
gboolean	gst_osscommon_merge_fixed_caps 	(GstOssCommon *common, GstCaps *caps);
	
gboolean 	gst_osscommon_sync_parms 	(GstOssCommon *common);

gboolean 	gst_osscommon_convert 		(GstOssCommon *common, 
						 GstFormat src_format, gint64 src_value,
		                       		 GstFormat *dest_format, gint64 *dest_value);

	

#endif /* __GST_OSSCOMMON_H__ */
