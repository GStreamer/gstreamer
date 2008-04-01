/* GStreamer A-Law to PCM conversion
 * Copyright (C) 2000 by Abramo Bagnara <abramo@alsa-project.org>
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

#ifndef __GST_ALAW_DECODE_H__
#define __GST_ALAW_DECODE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ALAW_DEC \
  (gst_alaw_dec_get_type())
#define GST_ALAW_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ALAW_DEC,GstALawDec))
#define GST_ALAW_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ALAW_DEC,GstALawDecClass))
#define GST_IS_ALAW_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ALAW_DEC))
#define GST_IS_ALAW_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ALAW_DEC))

typedef struct _GstALawDec GstALawDec;
typedef struct _GstALawDecClass GstALawDecClass;

struct _GstALawDec {
  GstElement element;

  GstPad *sinkpad,*srcpad;
  gint rate;
  gint channels;
};

struct _GstALawDecClass {
  GstElementClass parent_class;
};

GType gst_alaw_dec_get_type(void);

G_END_DECLS

#endif /* __GST_ALAW_DECODE_H__ */

