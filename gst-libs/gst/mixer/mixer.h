/* GStreamer Mixer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * mixer.h: mixer interface design
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

#ifndef __GST_MIXER_H__
#define __GST_MIXER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_MIXER \
  (gst_mixer_get_type ())
#define GST_MIXER(obj) \
  (GST_INTERFACE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MIXER, GstMixer))
#define GST_MIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MIXER, GstMixerClass))
#define GST_IS_MIXER(obj) \
  (GST_INTERFACE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MIXER))
#define GST_IS_MIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MIXER))
#define GST_MIXER_GET_CLASS(inst) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_MIXER, GstMixerClass))

/* I fully realise that this naming being used here is confusing.
 * A channel is referred to both as the number of simultaneous
 * sound streams the input can handle as well as the in-/output
 * itself. We need to fix this some day, I just cannot come up
 * with something better.
 */

#define GST_MIXER_CHANNEL_INPUT  (1<<0)
#define GST_MIXER_CHANNEL_OUTPUT (1<<1)
#define GST_MIXER_CHANNEL_MUTE   (1<<2)
#define GST_MIXER_CHANNEL_RECORD (1<<3)

typedef struct _GstMixerChannel {
  gchar *label;
  gint   num_channels,
         flags,
	 min_volume, max_volume;
} GstMixerChannel;

#define GST_MIXER_CHANNEL_HAS_FLAG(channel, flag) \
  ((channel)->flags & flag)

typedef struct _GstMixer GstMixer;

typedef struct _GstMixerClass {
  GTypeInterface klass;

  /* virtual functions */
  const GList *  (* list_channels) (GstMixer        *mixer);

  void           (* set_volume)    (GstMixer        *mixer,
				    GstMixerChannel *channel,
				    gint            *volumes);
  void           (* get_volume)    (GstMixer        *mixer,
				    GstMixerChannel *channel,
				    gint            *volumes);

  void           (* set_mute)      (GstMixer        *mixer,
				    GstMixerChannel *channel,
				    gboolean         mute);
  void           (* set_record)    (GstMixer        *mixer,
				    GstMixerChannel *channel,
				    gboolean         record);
} GstMixerClass;

GType		gst_mixer_get_type	(void);

/* virtual class function wrappers */
const GList *	gst_mixer_list_channels	(GstMixer        *mixer);
void		gst_mixer_set_volume	(GstMixer        *mixer,
					 GstMixerChannel *channel,
					 gint            *volumes);
void		gst_mixer_get_volume	(GstMixer        *mixer,
					 GstMixerChannel *channel,
					 gint            *volumes);
void		gst_mixer_set_mute	(GstMixer        *mixer,
					 GstMixerChannel *channel,
					 gboolean         mute);
void		gst_mixer_set_record	(GstMixer        *mixer,
					 GstMixerChannel *channel,
					 gboolean         record);

G_END_DECLS

#endif /* __GST_MIXER_H__ */
