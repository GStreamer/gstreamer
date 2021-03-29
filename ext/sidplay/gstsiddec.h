/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_SIDDEC_H__
#define __GST_SIDDEC_H__

#include <stdlib.h>
#include <sidplay/player.h>

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SIDDEC \
  (gst_siddec_get_type())
#define GST_SIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SIDDEC,GstSidDec))
#define GST_SIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SIDDEC,GstSidDecClass))
#define GST_IS_SIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SIDDEC))
#define GST_IS_SIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SIDDEC))

typedef struct _GstSidDec GstSidDec;
typedef struct _GstSidDecClass GstSidDecClass;

struct _GstSidDec {
  GstElement     element;

  /* pads */
  GstPad        *sinkpad, 
                *srcpad;

  gboolean       have_group_id;
  guint          group_id;

  guchar        *tune_buffer;
  gint           tune_len;
  gint           tune_number;
  guint64        total_bytes;

  emuEngine     *engine;
  sidTune       *tune;
  emuConfig     *config;

  guint         blocksize;
};

struct _GstSidDecClass {
  GstElementClass parent_class;
};

GType gst_siddec_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (siddec);

G_END_DECLS

#endif /* __GST_SIDDEC_H__ */
