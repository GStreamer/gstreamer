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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_AUTO_COLOR_SPACE_H__
#define __GST_AUTO_COLOR_SPACE_H__

#include <gst/gst.h>
#include <gst/gstbin.h>
#include "gstautoconvert.h"

G_BEGIN_DECLS
#define GST_TYPE_AUTO_COLOR_SPACE 	        	(gst_auto_color_space_get_type())
#define GST_AUTO_COLOR_SPACE(obj)	            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AUTO_COLOR_SPACE,GstAutoColorSpace))
#define GST_AUTO_COLOR_SPACE_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AUTO_COLOR_SPACE,GstAutoColorSpaceClass))
#define GST_IS_AUTO_COLOR_SPACE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AUTO_COLOR_SPACE))
#define GST_IS_AUTO_COLOR_SPACE_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AUTO_COLOR_SPACE))
typedef struct _GstAutoColorSpace GstAutoColorSpace;
typedef struct _GstAutoColorSpaceClass GstAutoColorSpaceClass;

struct _GstAutoColorSpace
{
  /*< private > */
  GstBin bin;                   /* we extend GstBin */

  GstElement *autoconvert;
  GstPad *sinkpad;
  GstPad *srcpad;
};

struct _GstAutoColorSpaceClass
{
  GstBinClass parent_class;
};

GType gst_auto_color_space_get_type (void);

G_END_DECLS
#endif /* __GST_AUTO_COLOR_SPACE_H__ */
