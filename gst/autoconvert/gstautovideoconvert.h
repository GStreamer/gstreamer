/* GStreamer
 * Copyright 2010 ST-Ericsson SA 
 *  @author: Benjamin Gaignard <benjamin.gaignard@stericsson.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_AUTO_VIDEO_CONVERT_H__
#define __GST_AUTO_VIDEO_CONVERT_H__

#include <gst/gst.h>
#include <gst/gstbin.h>
#include "gstautoconvert.h"

G_BEGIN_DECLS
#define GST_TYPE_AUTO_VIDEO_CONVERT 	        	(gst_auto_video_convert_get_type())
#define GST_AUTO_VIDEO_CONVERT(obj)	            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AUTO_VIDEO_CONVERT,GstAutoVideoConvert))
#define GST_AUTO_VIDEO_CONVERT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AUTO_VIDEO_CONVERT,GstAutoVideoConvertClass))
#define GST_IS_AUTO_VIDEO_CONVERT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AUTO_VIDEO_CONVERT))
#define GST_IS_AUTO_VIDEO_CONVERT_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AUTO_VIDEO_CONVERT))
typedef struct _GstAutoVideoConvert GstAutoVideoConvert;
typedef struct _GstAutoVideoConvertClass GstAutoVideoConvertClass;

struct _GstAutoVideoConvert
{
  /*< private > */
  GstBin bin;                   /* we extend GstBin */

  GstElement *autoconvert;
  GstPad *sinkpad;
  GstPad *srcpad;
};

struct _GstAutoVideoConvertClass
{
  GstBinClass parent_class;
};

GType gst_auto_video_convert_get_type (void);

G_END_DECLS
#endif /* __GST_AUTO_VIDEO_CONVERT_H__ */
