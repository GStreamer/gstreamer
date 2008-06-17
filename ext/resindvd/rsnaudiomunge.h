/* GStreamer
 * Copyright (C) 2008 Jan Schmidt <thaytan@noraisin.net>
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

#ifndef __RSNAUDIOMUNGE_H__
#define __RSNAUDIOMUNGE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define RSN_TYPE_AUDIOMUNGE (rsn_audiomunge_get_type())
#define RSN_AUDIOMUNGE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),RSN_TYPE_AUDIOMUNGE,RsnAudioMunge))
#define RSN_AUDIOMUNGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),RSN_TYPE_AUDIOMUNGE,RsnAudioMungeClass))
#define RSN_IS_AUDIOMUNGE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),RSN_TYPE_AUDIOMUNGE))
#define RSN_IS_AUDIOMUNGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),RSN_TYPE_AUDIOMUNGE))

typedef struct _RsnAudioMunge      RsnAudioMunge;
typedef struct _RsnAudioMungeClass RsnAudioMungeClass;

struct _RsnAudioMunge
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstSegment sink_segment;
  gboolean have_audio;
  gboolean in_still;
};

struct _RsnAudioMungeClass 
{
  GstElementClass parent_class;
};

GType rsn_audiomunge_get_type (void);

G_END_DECLS

#endif /* __RSNAUDIOMUNGE_H__ */
