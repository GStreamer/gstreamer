/* GStreamer
 * Copyright (C) <2009> Jan Schmidt <thaytan@noraisin.net>
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

#ifndef __RSNAUDIODEC_H__
#define __RSNAUDIODEC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define RSN_TYPE_AUDIODEC               (rsn_audiodec_get_type())
#define RSN_AUDIODEC(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),RSN_TYPE_AUDIODEC,RsnAudioDec))
#define RSN_AUDIODEC_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),RSN_TYPE_AUDIODEC,RsnAudioDecClass))
#define RSN_IS_AUDIODEC(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),RSN_TYPE_AUDIODEC))
#define RSN_IS_AUDIODEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),RSN_TYPE_AUDIODEC))

GType           rsn_audiodec_get_type           (void) G_GNUC_CONST;

typedef struct _RsnAudioDec             RsnAudioDec;
typedef struct _RsnAudioDecClass        RsnAudioDecClass;

struct _RsnAudioDec {
  GstBin element;

  /* Our sink and source pads */
  GstGhostPad *sinkpad;
  GstGhostPad *srcpad;

  GstPadEventFunction sink_event_func;

  GstElement *current_decoder;
};

struct _RsnAudioDecClass {
  GstBinClass parent_class;
};

G_END_DECLS

#endif /* __RSNAUDIODEC_H__ */
