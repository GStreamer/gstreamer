/*
 * GStreamer
 * Copyright (c) 2005 INdT.
 * @author Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * @author Philippe Khalaf <burger@speedy.org>
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

#ifndef __GST_MIM_DEC_H__
#define __GST_MIM_DEC_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <mimic.h>

G_BEGIN_DECLS
#define GST_TYPE_MIM_DEC \
  (gst_mim_dec_get_type())
#define GST_MIM_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MIM_DEC,GstMimDec))
#define GST_MIM_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MIM_DEC,GstMimDecClass))
#define GST_IS_MIM_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MIM_DEC))
#define GST_IS_MIM_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MIM_DEC))
typedef struct _GstMimDec GstMimDec;
typedef struct _GstMimDecClass GstMimDecClass;

struct _GstMimDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* Protected by stream lock */
  GstAdapter *adapter;
  MimCtx *dec;
  gint buffer_size;
  gboolean need_segment;
};

struct _GstMimDecClass
{
  GstElementClass parent_class;
};

GType gst_mim_dec_get_type (void);

G_END_DECLS
#endif /* __GST_MIM_DEC_H__ */
