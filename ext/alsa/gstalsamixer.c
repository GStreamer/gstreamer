/* GStreamer ALSA mixer implementation
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

#include "gstalsamixer.h"

static gboolean		gst_alsa_mixer_supported	(GstInterface   *iface,
							 GType           iface_type);

static const GList *	gst_alsa_mixer_list_tracks	(GstMixer       *mixer);

static void		gst_alsa_mixer_set_volume	(GstMixer       *mixer,
							 GstMixerTrack  *track,
							 gint           *volumes);
static void		gst_alsa_mixer_get_volume	(GstMixer       *mixer,
							 GstMixerTrack  *track,
							 gint           *volumes);

static void		gst_alsa_mixer_set_record	(GstMixer       *mixer,
							 GstMixerTrack  *track,
							 gboolean        record);
static void		gst_alsa_mixer_set_mute		(GstMixer       *mixer,
							 GstMixerTrack  *track,
							 gboolean        mute);

GstMixerTrack *
gst_alsa_mixer_track_new (GstAlsa *alsa,
                          gint track_num,
                          gint channels,
                          gint flags)
{
  gint i;
  long min, max;
  snd_mixer_elem_t *element = NULL;
  GstMixerTrack *track = (GstMixerTrack *) g_new (GstAlsaMixerTrack, 1);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  /* find a pointer to the track_num-th element in the mixer. */
  i = 0;
  element = snd_mixer_first_elem(alsa->mixer_handle);
  while (i++ < track_num) element = snd_mixer_elem_next(element);

  /* set basic information */
  track->label = g_strdup_printf("%s", snd_mixer_selem_get_name(element));
  track->num_channels = channels;
  track->flags = flags;
  alsa_track->element = element;
  alsa_track->track_num = track_num;

  /* set volume information */
  snd_mixer_selem_get_playback_volume_range(element, &min, &max);
  track->min_volume = (gint) min;
  track->max_volume = (gint) max;

  snd_mixer_selem_get_capture_volume_range(element, &min, &max);
  alsa_track->min_rec_volume = (gint) min;
  alsa_track->max_rec_volume = (gint) max;

  for (i = 0; i < channels; i++) {
    long tmp;
    if (snd_mixer_selem_has_playback_channel(element, i)) {
      snd_mixer_selem_get_playback_volume(element, i, &tmp);
      alsa_track->volumes[i] = (gint) tmp;
    } else if (snd_mixer_selem_has_capture_channel(element, i)) {
      snd_mixer_selem_get_capture_volume(element, i, &tmp);
      alsa_track->volumes[i] = (gint) tmp;
    }
  }

  return track;
}

void
gst_alsa_mixer_track_free (GstMixerTrack *track)
{
  g_free (track->label);
  g_free (track);
}

void
gst_alsa_interface_init (GstInterfaceClass *klass)
{
  /* default virtual functions */
  klass->supported = gst_alsa_mixer_supported;
}

void
gst_alsa_mixer_interface_init (GstMixerClass *klass)
{
  /* default virtual functions */
  klass->list_tracks = gst_alsa_mixer_list_tracks;
  klass->set_volume = gst_alsa_mixer_set_volume;
  klass->get_volume = gst_alsa_mixer_get_volume;
  klass->set_mute = gst_alsa_mixer_set_mute;
  klass->set_record = gst_alsa_mixer_set_record;
}

static gboolean
gst_alsa_mixer_supported (GstInterface *iface, GType iface_type)
{
  g_assert (iface_type == GST_TYPE_MIXER);

  return (((gint) GST_ALSA (iface)->mixer_handle) != -1);
}

static const GList *
gst_alsa_mixer_list_tracks (GstMixer *mixer)
{
  GstAlsa *alsa = GST_ALSA (mixer);

  g_return_val_if_fail (((gint) alsa->mixer_handle) != -1, NULL);

  return (const GList *) GST_ALSA (mixer)->tracklist;
}

static void
gst_alsa_mixer_get_volume (GstMixer *mixer,
                           GstMixerTrack *track,
                           gint *volumes)
{
  gint i;
  GstAlsa *alsa = GST_ALSA (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (((gint) alsa->mixer_handle) != -1);

  if (track->flags & GST_MIXER_TRACK_MUTE) {
    for (i = 0; i < track->num_channels; i++)
      volumes[i] = alsa_track->volumes[i];
  } else {
    for (i = 0; i < track->num_channels; i++) {
      long tmp;
      if (snd_mixer_selem_has_playback_channel(alsa_track->element, i)) {
        snd_mixer_selem_get_playback_volume(alsa_track->element, i, &tmp);
        volumes[i] = (gint) tmp;
      } else if (snd_mixer_selem_has_capture_channel(alsa_track->element, i)) {
        snd_mixer_selem_get_capture_volume(alsa_track->element, i, &tmp);
        volumes[i] = (gint) tmp;
      }
    }
  }
}

static void
gst_alsa_mixer_set_volume (GstMixer *mixer,
                           GstMixerTrack *track,
                           gint *volumes)
{
  gint i;
  GstAlsa *alsa = GST_ALSA (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (((gint) alsa->mixer_handle) != -1);

  /* only set the volume with ALSA lib if the track isn't muted. */
  if (! (track->flags & GST_MIXER_TRACK_MUTE)) {
    for (i = 0; i < track->num_channels; i++) {
      if (snd_mixer_selem_has_playback_channel(alsa_track->element, i))
        snd_mixer_selem_set_playback_volume(alsa_track->element, i, (long) volumes[i]);
      else if (snd_mixer_selem_has_capture_channel(alsa_track->element, i))
        snd_mixer_selem_set_capture_volume(alsa_track->element, i, (long) volumes[i]);
    }
  }

  for (i = 0; i < track->num_channels; i++)
    alsa_track->volumes[i] = volumes[i];
}

static void
gst_alsa_mixer_set_mute (GstMixer *mixer,
                         GstMixerTrack *track,
                         gboolean mute)
{
  gint i;
  GstAlsa *alsa = GST_ALSA (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (((gint) alsa->mixer_handle) != -1);

  if (mute) {
    track->flags |= GST_MIXER_TRACK_MUTE;

    for (i = 0; i < track->num_channels; i++) {
      if (snd_mixer_selem_has_capture_channel(alsa_track->element, i))
        snd_mixer_selem_set_capture_volume(alsa_track->element, i, 0);
      else if (snd_mixer_selem_has_playback_channel(alsa_track->element, i))
        snd_mixer_selem_set_playback_volume(alsa_track->element, i, 0);
    }
  } else {
    track->flags &= ~GST_MIXER_TRACK_MUTE;

    for (i = 0; i < track->num_channels; i++) {
      if (snd_mixer_selem_has_capture_channel(alsa_track->element, i))
        snd_mixer_selem_set_capture_volume(alsa_track->element, i,
                                           alsa_track->volumes[i]);
      else if (snd_mixer_selem_has_playback_channel(alsa_track->element, i))
        snd_mixer_selem_set_playback_volume(alsa_track->element, i,
                                            alsa_track->volumes[i]);
    }
  }
}

static void
gst_alsa_mixer_set_record (GstMixer *mixer,
                           GstMixerTrack *track,
                           gboolean record)
{
  GstAlsa *alsa = GST_ALSA (mixer);
  /* GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track; */

  g_return_if_fail (((gint) alsa->mixer_handle) != -1);

  if (record) {
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }
}

void
gst_alsa_mixer_build_list (GstAlsa *alsa)
{
  gint i, count, err;
  snd_mixer_elem_t *element;

  /* set up this mixer. */

  if ((gint) alsa->mixer_handle == -1) {
    if ((err = snd_mixer_open(&alsa->mixer_handle, 0)) != 0) {
      GST_ERROR_OBJECT (GST_OBJECT (alsa), "Cannot open mixer device.");
      alsa->mixer_handle = (snd_mixer_t *) -1;
      return;
    }
  }

  if ((err = snd_mixer_attach(alsa->mixer_handle, alsa->device)) != 0) {
    GST_ERROR_OBJECT (GST_OBJECT (alsa),
                      "Cannot attach mixer to sound device '%s'.",
                      alsa->device);
    return;
  }

  if ((err = snd_mixer_selem_register(alsa->mixer_handle, NULL, NULL)) != 0) {
    GST_ERROR_OBJECT (GST_OBJECT (alsa), "Cannot register mixer elements.");
    return;
  }

  if ((err = snd_mixer_load(alsa->mixer_handle)) != 0) {
    GST_ERROR_OBJECT (GST_OBJECT (alsa), "Cannot load mixer settings.");
    return;
  }

  count = snd_mixer_get_count(alsa->mixer_handle);
  element = snd_mixer_first_elem(alsa->mixer_handle);

  /* build track list */

  for (i = 0; i < count; i++) {
    GstMixerTrack *track;
    gint channels = 0;
    gboolean input = FALSE;
    gint flags = 0;

    /* find out if this is a capture track */
    if (snd_mixer_selem_has_capture_channel(element, 0) ||
        snd_mixer_selem_has_capture_switch(element) ||
        snd_mixer_selem_is_capture_mono(element)) input = TRUE;

    flags = (input ? GST_MIXER_TRACK_INPUT : GST_MIXER_TRACK_OUTPUT);

    if (input) {
      while (snd_mixer_selem_has_capture_channel(element, channels))
        channels++;
    } else {
      while (snd_mixer_selem_has_playback_channel(element, channels))
        channels++;
    }

    /* add track to list */
    track = gst_alsa_mixer_track_new (alsa, i, channels, flags);
    alsa->tracklist = g_list_append (alsa->tracklist, track);

    element = snd_mixer_elem_next(element);
  }
}

void
gst_alsa_mixer_free_list (GstAlsa *alsa)
{
  g_return_if_fail (((gint) alsa->mixer_handle) != -1);

  g_list_foreach (alsa->tracklist, (GFunc) gst_alsa_mixer_track_free, NULL);
  g_list_free (alsa->tracklist);
  alsa->tracklist = NULL;

  snd_mixer_close (alsa->mixer_handle);
  alsa->mixer_handle = (snd_mixer_t *) -1;
}
