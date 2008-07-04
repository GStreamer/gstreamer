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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_MIMENC_H__
#define __GST_MIMENC_H__

#include <glib.h>
#include <gst/gst.h>
#include <mimic.h>

G_BEGIN_DECLS

#define GST_TYPE_MIMENC \
  (gst_mimenc_get_type())
#define GST_MIMENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MIMENC,GstMimEnc))
#define GST_MIMENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MIMENC,GstMimEnc))
#define GST_IS_MIMENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MIMENC))
#define GST_IS_MIMENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MIMENC))

typedef struct _GstMimEnc      GstMimEnc;
typedef struct _GstMimEncClass GstMimEncClass;

struct _GstMimEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  MimCtx *enc;

  MimicResEnum res;
  gint buffer_size;
  guint32 frames;
  guint16 height, width;
};

struct _GstMimEncClass
{
  GstElementClass parent_class;
};

GType gst_mimenc_get_type (void);

G_END_DECLS

#endif /* __GST_MIMENC_H__ */
