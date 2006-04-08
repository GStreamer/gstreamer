/* ALSA mixer track implementation.
 * Copyright (C) 2003 Leif Johnson <leif@ambient.2y.net>
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

#include <gst/gst-i18n-plugin.h>

#include "gstalsamixertrack.h"

static void gst_alsa_mixer_track_init (GstAlsaMixerTrack * alsa_track);
static void gst_alsa_mixer_track_class_init (gpointer g_class,
    gpointer class_data);

static GstMixerTrackClass *parent_class = NULL;

GType
gst_alsa_mixer_track_get_type (void)
{
  static GType track_type = 0;

  if (!track_type) {
    static const GTypeInfo track_info = {
      sizeof (GstAlsaMixerTrackClass),
      NULL,
      NULL,
      gst_alsa_mixer_track_class_init,
      NULL,
      NULL,
      sizeof (GstAlsaMixerTrack),
      0,
      (GInstanceInitFunc) gst_alsa_mixer_track_init,
    };

    track_type =
        g_type_register_static (GST_TYPE_MIXER_TRACK, "GstAlsaMixerTrack",
        &track_info, 0);
  }

  return track_type;
}

static void
gst_alsa_mixer_track_class_init (gpointer g_class, gpointer class_data)
{
  parent_class = g_type_class_peek_parent (g_class);
}

static void
gst_alsa_mixer_track_init (GstAlsaMixerTrack * alsa_track)
{
}

GstMixerTrack *
gst_alsa_mixer_track_new (snd_mixer_elem_t * element,
    gint num, gint track_num, gint channels, gint flags, gint alsa_flags)
{
  gint i;
  long min = 0, max = 0;
  struct
  {
    gchar *orig, *trans;
  } alsa_track_labels[] = {
    {
    "Master", _("Master")}, {
    "Bass", _("Bass")}, {
    "Treble", _("Treble")}, {
    "PCM", _("PCM")}, {
    "Synth", _("Synth")}, {
    "Line", _("Line-in")}, {
    "CD", _("CD")}, {
    "Mic", _("Microphone")}, {
    "PC Speaker", _("PC Speaker")}, {
    "Playback", _("Playback")}, {
    "Capture", _("Capture")}, {
    NULL, NULL}
  };

  GstMixerTrack *track = g_object_new (GST_ALSA_MIXER_TRACK_TYPE, NULL);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  /* set basic information */
  if (num == 0)
    track->label = g_strdup (snd_mixer_selem_get_name (element));
  else
    track->label = g_strdup_printf ("%s %d",
        snd_mixer_selem_get_name (element), num + 1);
  i = 0;
  while (alsa_track_labels[i].orig != NULL) {
    if (!g_utf8_collate (snd_mixer_selem_get_name (element),
            alsa_track_labels[i].orig)) {
      g_free (track->label);
      if (num == 0)
        track->label = g_strdup (alsa_track_labels[i].trans);
      else
        track->label = g_strdup_printf ("%s %d",
            alsa_track_labels[i].trans, num);
      break;
    }
    i++;
  }
  track->num_channels = channels;
  track->flags = flags;
  alsa_track->element = element;
  alsa_track->alsa_flags = alsa_flags;
  alsa_track->track_num = track_num;

  /* set volume information */
  if (channels) {
    if (alsa_flags & GST_ALSA_MIXER_TRACK_PLAYBACK) {
      snd_mixer_selem_get_playback_volume_range (element, &min, &max);
    } else if (alsa_flags & GST_ALSA_MIXER_TRACK_CAPTURE) {
      snd_mixer_selem_get_capture_volume_range (element, &min, &max);
    }
  }
  track->min_volume = (gint) min;
  track->max_volume = (gint) max;

  for (i = 0; i < channels; i++) {
    long tmp = 0;

    if (alsa_flags & GST_ALSA_MIXER_TRACK_PLAYBACK) {
      snd_mixer_selem_get_playback_volume (element, i, &tmp);
    } else if (alsa_flags & GST_ALSA_MIXER_TRACK_CAPTURE) {
      snd_mixer_selem_get_capture_volume (element, i, &tmp);
    }
    alsa_track->volumes[i] = (gint) tmp;
  }

  if (snd_mixer_selem_has_playback_switch (element)) {
    int val = 1;

    snd_mixer_selem_get_playback_switch (element, 0, &val);
    if (!val)
      track->flags |= GST_MIXER_TRACK_MUTE;
  }

  if (flags & GST_MIXER_TRACK_INPUT) {
    int val = 0;

    snd_mixer_selem_get_capture_switch (element, 0, &val);
    if (val)
      track->flags |= GST_MIXER_TRACK_RECORD;
  }

  return track;
}
