/* GStreamer
 *
 *  Copyright 2007 Collabora Ltd
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  Copyright 2007-2008 Nokia
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


#ifndef __GST_AUTO_CONVERT_H__
#define __GST_AUTO_CONVERT_H__

#include <gst/gst.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS
#define GST_TYPE_AUTO_CONVERT 	        	(gst_auto_convert_get_type())
#define GST_AUTO_CONVERT(obj)	                (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AUTO_CONVERT,GstAutoConvert))
#define GST_AUTO_CONVERT_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AUTO_CONVERT,GstAutoConvertClass))
#define GST_IS_AUTO_CONVERT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AUTO_CONVERT))
#define GST_IS_AUTO_CONVERT_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AUTO_CONVERT))
typedef struct _GstAutoConvert GstAutoConvert;
typedef struct _GstAutoConvertClass GstAutoConvertClass;

struct _GstAutoConvert
{
  /*< private >*/
  GstBin bin;                   /* we extend GstBin */

  volatile GList *factories;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* Have to be set all at once
   * Protected by the object lock and the stream lock
   * Both must be held to modify these
   */
  GstElement *current_subelement;
  GstPad *current_internal_srcpad;
  GstPad *current_internal_sinkpad;
};

struct _GstAutoConvertClass
{
  GstBinClass parent_class;
};

GType gst_auto_convert_get_type (void);

G_END_DECLS
#endif /* __GST_AUTO_CONVERT_H__ */
