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

#ifndef __GST_MIM_ENC_H__
#define __GST_MIM_ENC_H__

#include <glib.h>
#include <gst/gst.h>
#include <mimic.h>

G_BEGIN_DECLS
#define GST_TYPE_MIM_ENC \
  (gst_mim_enc_get_type())
#define GST_MIM_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MIM_ENC,GstMimEnc))
#define GST_MIM_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MIM_ENC,GstMimEncClass))
#define GST_IS_MIM_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MIM_ENC))
#define GST_IS_MIM_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MIM_ENC))
typedef struct _GstMimEnc GstMimEnc;
typedef struct _GstMimEncClass GstMimEncClass;

struct _GstMimEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* Protected by the object lock */
  MimCtx *enc;

  MimicResEnum res;
  gint buffer_size;
  guint32 frames;
  guint16 height, width;

  gboolean paused_mode;
  GstSegment segment;
  GstEvent *pending_segment;
  GstClockTime last_buffer;
  GstClockID clock_id;
  gboolean stop_paused_mode;
};

struct _GstMimEncClass
{
  GstElementClass parent_class;
};

GType gst_mim_enc_get_type (void);

G_END_DECLS
#endif /* __GST_MIM_ENC_H__ */
