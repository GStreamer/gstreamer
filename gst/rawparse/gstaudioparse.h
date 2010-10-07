/* GStreamer
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstaudioparse.h:
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

#ifndef __GST_AUDIO_PARSE_H__
#define __GST_AUDIO_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>

#include "gstrawparse.h"

#define GST_TYPE_AUDIO_PARSE \
  (gst_audio_parse_get_type())
#define GST_AUDIO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_PARSE,GstAudioParse))
#define GST_AUDIO_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_PARSE,GstAudioParseClass))
#define GST_IS_AUDIO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_PARSE))
#define GST_IS_AUDIO_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_PARSE))

typedef struct _GstAudioParse GstAudioParse;
typedef struct _GstAudioParseClass GstAudioParseClass;

struct _GstAudioParse
{
  GstRawParse parent;

  /* properties */
  gint format;
  gint channels;
  gint width;
  gint depth;
  gint signedness;
  gint endianness;
  GValueArray *channel_positions;
};

struct _GstAudioParseClass
{
  GstRawParseClass parent_class;
};


GType gst_audio_parse_get_type (void);

#endif /*  __GST_AUDIO_PARSE_H__ */
