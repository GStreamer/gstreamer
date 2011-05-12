/* GStreamer
 * Copyright (C) <2007> Julien Moutte <julien@fluendo.com>
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
 
#ifndef __MPEG4VIDEOPARSE_H__
#define __MPEG4VIDEOPARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

#include "mpeg4parse.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEG4VIDEOPARSE            (gst_mpeg4vparse_get_type())
#define GST_MPEG4VIDEOPARSE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                GST_TYPE_MPEG4VIDEOPARSE, GstMpeg4VParse))
#define GST_MPEG4VIDEOPARSE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                GST_TYPE_MPEG4VIDEOPARSE, GstMpeg4VParseClass))
#define GST_MPEG4VIDEOPARSE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                GST_TYPE_MPEG4VIDEOPARSE, GstMpeg4VParseClass))
#define GST_IS_MPEG4VIDEOPARSE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                GST_TYPE_MPEG4VIDEOPARSE))
#define GST_IS_MPEG4VIDEOPARSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                GST_TYPE_MPEG4VIDEOPARSE))

typedef struct _GstMpeg4VParse GstMpeg4VParse;
typedef struct _GstMpeg4VParseClass GstMpeg4VParseClass;

struct _GstMpeg4VParse {
  GstBaseParse element;

  GstClockTime last_report;

  /* parse state */
  gint last_sc;
  gint vop_offset;
  gint vos_offset;
  gint vo_offset;
  gboolean intra_frame;
  gboolean update_caps;

  GstBuffer *config;
  guint8 profile;
  MPEG4Params params;

  /* properties */
  gboolean drop;
  guint interval;
};

struct _GstMpeg4VParseClass {
  GstBaseParseClass parent_class;
};

GType gst_mpeg4vparse_get_type (void);

G_END_DECLS

#endif /* __MPEG4VIDEOPARSE_H__ */
