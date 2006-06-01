/* GStreamer
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2004 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_DIRACDEC_H__
#define __GST_DIRACDEC_H__

#include <gst/gst.h>

#include <libdirac_decoder/decoder_types.h>
#include <libdirac_decoder/dirac_parser.h>

G_BEGIN_DECLS

#define GST_TYPE_DIRACDEC \
  (gst_diracdec_get_type())
#define GST_DIRACDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRACDEC,GstDiracDec))
#define GST_DIRACDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRACDEC,GstDiracDecClass))
#define GST_IS_DIRACDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRACDEC))
#define GST_IS_DIRACDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRACDEC))

typedef struct _GstDiracDec GstDiracDec;
typedef struct _GstDiracDecClass GstDiracDecClass;

struct _GstDiracDec
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  dirac_decoder_t *decoder;

  /* size is size of buf */
  gint width, height, size;
  gdouble fps;
  guint32 fcc;
};

struct _GstDiracDecClass
{
  GstElementClass parent_class;
};

GType gst_diracdec_get_type (void);

G_END_DECLS

#endif /* __GST_DIRACDEC_H__ */
