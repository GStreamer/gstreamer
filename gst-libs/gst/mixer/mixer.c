/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * mixer.c: mixer design virtual class function wrappers
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/mixer/mixer.h>

static void 	gst_mixer_class_init	(GstMixerClass *klass);

GType
gst_mixer_get_type (void)
{
  static GType gst_mixer_type = 0;

  if (!gst_mixer_type) {
    static const GTypeInfo gst_mixer_info = {
      sizeof (GstMixerClass),
      (GBaseInitFunc) gst_mixer_class_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    gst_mixer_type = g_type_register_static (G_TYPE_INTERFACE,
					     "GstMixer",
					     &gst_mixer_info, 0);
    g_type_interface_add_prerequisite (gst_mixer_type,
				       GST_TYPE_INTERFACE);
  }

  return gst_mixer_type;
}

static void
gst_mixer_class_init (GstMixerClass *klass)
{
  /* default virtual functions */
  klass->list_channels = NULL;
  klass->set_volume = NULL;
  klass->get_volume = NULL;
  klass->set_mute = NULL;
  klass->set_record = NULL;
}

const GList *
gst_mixer_list_channels	(GstMixer *mixer)
{
  GstMixerClass *klass = GST_MIXER_GET_CLASS (mixer);

  if (klass->list_channels) {
    return klass->list_channels (mixer);
  }

  return NULL;
}

void
gst_mixer_set_volume (GstMixer        *mixer,
		      GstMixerChannel *channel,
		      gint            *volumes)
{
  GstMixerClass *klass = GST_MIXER_GET_CLASS (mixer);

  if (klass->set_volume) {
    klass->set_volume (mixer, channel, volumes);
  }
}

void
gst_mixer_get_volume (GstMixer        *mixer,
		      GstMixerChannel *channel,
		      gint            *volumes)
{
  GstMixerClass *klass = GST_MIXER_GET_CLASS (mixer);

  if (klass->get_volume) {
    klass->get_volume (mixer, channel, volumes);
  } else {
    gint i;

    for (i = 0; i < channel->num_channels; i++) {
      volumes[i] = 0;
    }
  }
}

void
gst_mixer_set_mute (GstMixer        *mixer,
		    GstMixerChannel *channel,
		    gboolean         mute)
{
  GstMixerClass *klass = GST_MIXER_GET_CLASS (mixer);

  if (klass->set_mute) {
    klass->set_mute (mixer, channel, mute);
  }
}

void
gst_mixer_set_record (GstMixer        *mixer,
		      GstMixerChannel *channel,
		      gboolean         record)
{
  GstMixerClass *klass = GST_MIXER_GET_CLASS (mixer);

  if (klass->set_record) {
    klass->set_record (mixer, channel, record);
  }
}
