/* GStreamer Mixer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * mixeroptions.h: mixer track options object
 * This should be a subclass of MixerItem, along with MixerOptions,
 * but that's not possible because of API/ABI in 0.8.x. FIXME.
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

#ifndef __GST_MIXER_OPTIONS_H__
#define __GST_MIXER_OPTIONS_H__

#include <gst/gst.h>
#include <gst/mixer/mixertrack.h>

G_BEGIN_DECLS

#define GST_TYPE_MIXER_OPTIONS \
  (gst_mixer_options_get_type ())
#define GST_MIXER_OPTIONS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MIXER_OPTIONS, \
			       GstMixerOptions))
#define GST_MIXER_OPTIONS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MIXER_OPTIONS, \
			    GstMixerOptionsClass))
#define GST_IS_MIXER_OPTIONS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MIXER_OPTIONS))
#define GST_IS_MIXER_OPTIONS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MIXER_OPTIONS))

typedef struct _GstMixerOptions {
  GstMixerTrack parent;

  /* list of strings */
  GList        *values;

  gpointer _gst_reserved[GST_PADDING];
} GstMixerOptions;

typedef struct _GstMixerOptionsClass {
  GstMixerTrackClass parent;

  void (* option_changed) (GstMixerOptions *opts,
			   gchar           *value);

  gpointer _gst_reserved[GST_PADDING];
} GstMixerOptionsClass;

GType		gst_mixer_options_get_type	(void);

G_END_DECLS

#endif /* __GST_MIXER_OPTIONS_H__ */
