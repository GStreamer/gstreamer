/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#ifndef __GST_VTDEC_H__
#define __GST_VTDEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "coremediactx.h"

G_BEGIN_DECLS

#define GST_VTDEC_CAST(obj) \
  ((GstVTDec *) (obj))
#define GST_VTDEC_CLASS_GET_CODEC_DETAILS(klass) \
  ((const GstVTDecoderDetails *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass), \
      GST_VTDEC_CODEC_DETAILS_QDATA))

typedef struct _GstVTDecoderDetails GstVTDecoderDetails;

typedef struct _GstVTDecClassParams GstVTDecClassParams;
typedef struct _GstVTDecClass GstVTDecClass;
typedef struct _GstVTDec GstVTDec;

struct _GstVTDecoderDetails
{
  const gchar * name;
  const gchar * element_name;
  const gchar * mimetype;
  VTFormatId format_id;
};

struct _GstVTDecClass
{
  GstElementClass parent_class;
};

struct _GstVTDec
{
  GstElement parent;

  const GstVTDecoderDetails * details;

  GstPad * sinkpad;
  GstPad * srcpad;

  GstCoreMediaCtx * ctx;

  GstVideoInfo vinfo;
  CMFormatDescriptionRef fmt_desc;
  VTDecompressionSessionRef session;

  GQueue * cur_outbufs;
  gboolean flush;
};

void gst_vtdec_register_elements (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_VTDEC_H__ */
