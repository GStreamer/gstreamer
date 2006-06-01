/* GStreamer
 *
 * gstv4ljpegsrc.h: V4L video source element for JPEG cameras
 *
 * Copyright (C) 2001-2005 Jan Schmidt <thaytan@mad.scientist.com>
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

#ifndef __GST_V4LJPEGSRC_H__
#define __GST_V4LJPEGSRC_H__

#include <gstv4lsrc.h>

G_BEGIN_DECLS
#define GST_TYPE_V4LJPEGSRC \
  (gst_v4ljpegsrc_get_type())
#define GST_V4LJPEGSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4LJPEGSRC,GstV4lJpegSrc))
#define GST_V4LJPEGSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4LJPEGSRC,GstV4lJpegSrcClass))
#define GST_IS_V4LJPEGSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4LJPEGSRC))
#define GST_IS_V4LJPEGSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4LJPEGSRC))
typedef struct _GstV4lJpegSrc GstV4lJpegSrc;
typedef struct _GstV4lJpegSrcClass GstV4lJpegSrcClass;

struct _GstV4lJpegSrc
{
  GstV4lSrc             v4lsrc;
  GstPadGetFunction     getfn;
  GstPadGetCapsFunction getcapsfn;
};

struct _GstV4lJpegSrcClass
{
  GstV4lSrcClass parent_class;
};

GType gst_v4ljpegsrc_get_type (void);

G_END_DECLS
#endif /* __GST_V4LJPEGSRC_H__ */
