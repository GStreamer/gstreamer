/* Copyright (C) 2004-2005 Michael Pyne <michael dot pyne at kdemail net>
 * Copyright (C) 2004-2006 Chris Lee <clee at kde org>
 * Copyright (C) 2007 Brian Koropoff <bkoropoff at gmail com>
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

#ifndef __GST_SPC_DEC_H__
#define __GST_SPC_DEC_H__

#include <gst/gst.h>

#include <openspc.h>

#include "tag.h"

G_BEGIN_DECLS

#define GST_TYPE_SPC_DEC \
  (gst_spc_dec_get_type())
#define GST_SPC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPC_DEC,GstSpcDec))
#define GST_SPC_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPC_DEC,GstSpcDecClass))
#define GST_IS_SPC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPC_DEC))
#define GST_IS_SPC_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPC_DEC))

typedef struct _GstSpcDec GstSpcDec;
typedef struct _GstSpcDecClass GstSpcDecClass;

struct _GstSpcDec
{
  GstElement  element;

  GstPad     *sinkpad;
  GstPad     *srcpad;

  GstBuffer  *buf;
  gboolean    initialized;
  gboolean    seeking;
  guint32     seekpoint;
  
  spc_tag_info tag_info;
  
  guint32 byte_pos;
};

struct _GstSpcDecClass
{
  GstElementClass parent_class;
};

GType gst_spc_dec_get_type(void);

G_END_DECLS

#endif /* __GST_SPC_DEC_H__ */
