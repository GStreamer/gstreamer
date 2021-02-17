/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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


#ifndef __GST_TWO_LAME_H__
#define __GST_TWO_LAME_H__


#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>

G_BEGIN_DECLS

#include <twolame.h>

#define GST_TYPE_TWO_LAME (gst_two_lame_get_type())
G_DECLARE_FINAL_TYPE (GstTwoLame, gst_two_lame, GST, TWO_LAME, GstAudioEncoder)

/**
 * GstTwoLame:
 *
 * Opaque data structure.
 */
struct _GstTwoLame {
  GstAudioEncoder element;

  gint samplerate;
  gint num_channels;
  gboolean float_input;
  gboolean setup;

  gint mode;
  gint psymodel;
  gint bitrate;
  gint padding;
  gboolean energy_level_extension;
  gint emphasis;
  gboolean error_protection;
  gboolean copyright;
  gboolean original;
  gboolean vbr;
  gfloat vbr_level;
  gfloat ath_level;
  gint vbr_max_bitrate;
  gboolean quick_mode;
  gint quick_mode_count;

  twolame_options *glopts;
};

GST_ELEMENT_REGISTER_DECLARE (twolamemp2enc);

G_END_DECLS


#endif /* __GST_TWO_LAME_H__ */
