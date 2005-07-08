/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosshelper.h: helper functions for OSS Device handling. This
 * set of functions takes care of device setting/getting,
 * opening/closing devices, parsing caps to OSS formats/settings
 * or the other way around, device probing, supported format
 * probing and mixer integration.
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

#ifndef __GST_OSS_HELPER_H__
#define __GST_OSS_HELPER_H__

#include <gst/gst.h>
#include <sys/types.h>

/* debugging category */
GST_DEBUG_CATEGORY_EXTERN (oss_debug);
#define GST_CAT_DEFAULT oss_debug

G_BEGIN_DECLS

enum {
  ARG_0,
  OSS_ARG_DEVICE,
  OSS_ARG_MIXER_DEVICE,
  OSS_ARG_DEVICE_NAME,
  OSS_ARG_0
};

typedef enum {
  GST_OSS_MODE_READ,
  GST_OSS_MODE_WRITE,
  GST_OSS_MODE_VOLUME,
  GST_OSS_MODE_MIXER
} GstOssOpenMode;

/*
 * Embed those two in whatever object you're creating.
 */

typedef struct _GstOssDeviceCombination {
  gchar *dsp, *mixer;
  dev_t dev;
} GstOssDeviceCombination;

typedef struct _GstOssDevice {
  /* device state */
  int		 fd;
  int		 caps; /* the capabilities */
  gint		 format;
  gint		 fragment;
  guint64	 fragment_time;
  gint		 fragment_size;
  GstOssOpenMode mode;
  GstCaps       *probed_caps;

  /* stats bytes per *second* */
  guint		 bps;

  /* sample width in bytes */
  guint		 sample_width;

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
} GstOssDevice;

/*
 * class/type/interface handling for mixer/device handling.
 */
void		gst_oss_add_mixer_type		(GType type);
void		gst_oss_add_device_properties	(GstElementClass * klass);
void		gst_oss_set_device_property	(GstElement * element,
						 GstOssDeviceCombination * c,
						 GstOssDevice * dev,
						 guint prop_id,
						 GParamSpec * pspec,
						 const GValue * value);
void		gst_oss_get_device_property	(GstElement * element,
						 GstOssDeviceCombination * c,
						 GstOssDevice * d,
						 guint prop_id,
						 GParamSpec * pspec,
						 GValue * value);

/*
 * device open/close.
 */
void		gst_oss_init	(GObject * obj,
				 GstOssDeviceCombination * c,
				 GstOssDevice * dev,
				 GstOssOpenMode mode);
void		gst_oss_dispose	(GstOssDeviceCombination * c,
				 GstOssDevice * dev);
gboolean	gst_oss_open	(GstElement * element,
				 GstOssDeviceCombination * c,
				 GstOssDevice * dev);
void		gst_oss_close	(GstOssDevice * dev);

/*
 * caps parsing/probing.
 */
gboolean 	gst_oss_parse_caps 		(GstOssDevice * dev,
						 const GstCaps * caps);
gboolean	gst_oss_merge_fixed_caps	(GstOssDevice * dev,
						 GstCaps * caps);

gboolean 	gst_oss_sync_parms 		(GstOssDevice * dev);
void		gst_oss_reset 			(GstOssDevice * dev);

gboolean 	gst_oss_convert 	 	(GstOssDevice * dev,
						 GstFormat src_format,
						 gint64 src_value,
		                       		 GstFormat * dest_format,
						 gint64 * dest_value);
void            gst_oss_probe_caps		(GstOssDevice * dev);

G_END_DECLS

#endif /* __GST_OSS_HELPER_H__ */
