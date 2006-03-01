/* ALSA mixer implementation.
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

/**
 * SECTION:element-alsamixer
 * @short_description: control properties of an audio device
 * @see_also: alsasink, alsasrc
 *
 * <refsect2>
 * <para>
 * This element controls various aspects such as the volume and balance
 * of an audio device using the ALSA api.
 * </para>
 * <para>
 * The application should query and use the interfaces provided by this 
 * element to control the device.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalsamixer.h"


/* First some utils, then the mixer implementation */

static gboolean
gst_alsa_mixer_open (GstAlsaMixer * mixer)
{
  gint err, devicenum;

  g_return_val_if_fail (mixer->handle == NULL, FALSE);

  /* open and initialize the mixer device */
  err = snd_mixer_open (&mixer->handle, 0);
  if (err < 0 || mixer->handle == NULL) {
    GST_WARNING ("Cannot open empty mixer.");
    mixer->handle = NULL;
    return FALSE;
  }

  /* hack hack hack hack hack!!!!! */
  if (strncmp (mixer->device, "default", 7) == 0) {
    /* hack! */
    g_free (mixer->device);
    mixer->device = g_strdup ("hw:0");
  } else if (strncmp (mixer->device, "hw:", 3) == 0) {
    /* ok */
  } else if (strncmp (mixer->device, "plughw:", 7) == 0) {
    gchar *freeme = mixer->device;

    mixer->device = g_strdup (freeme + 4);
    g_free (freeme);
  } else {
    goto error;
  }

  if (strchr (mixer->device, ','))
    strchr (mixer->device, ',')[0] = '\0';

  if ((err = snd_mixer_attach (mixer->handle, mixer->device)) < 0) {
    GST_WARNING ("Cannot open mixer for sound device `%s'.", mixer->device);
    goto error;
  }

  if ((err = snd_mixer_selem_register (mixer->handle, NULL, NULL)) < 0) {
    GST_WARNING ("Cannot register mixer elements.");
    goto error;
  }

  if ((err = snd_mixer_load (mixer->handle)) < 0) {
    GST_WARNING ("Cannot load mixer settings.");
    goto error;
  }

  /* I don't know how to get a device name from a mixer handle. So on
   * to the ugly hacks here, then... */
  if (sscanf (mixer->device, "hw:%d", &devicenum) == 1) {
    gchar *name;

    if (!snd_card_get_name (devicenum, &name)) {
      mixer->cardname = g_strdup (name);
      free (name);
      GST_DEBUG ("Card name = %s", GST_STR_NULL (mixer->cardname));
    }
  }

  GST_INFO ("Successfully opened mixer for device `%s'.", mixer->device);

  return TRUE;

error:
  snd_mixer_close (mixer->handle);
  mixer->handle = NULL;
  return FALSE;
}

static void
gst_alsa_mixer_ensure_track_list (GstAlsaMixer * mixer)
{
  gint i, count;
  snd_mixer_elem_t *element;
  GstMixerTrack *track;
  GstMixerOptions *opts;
  gboolean first = TRUE;

  g_return_if_fail (mixer->handle != NULL);

  if (mixer->tracklist)
    return;

  count = snd_mixer_get_count (mixer->handle);
  element = snd_mixer_first_elem (mixer->handle);

  /* build track list */
  for (i = 0; i < count; i++) {
    GList *item;
    gint channels = 0, samename = 0;
    gint flags = GST_MIXER_TRACK_OUTPUT;
    gboolean got_it = FALSE;

    if (snd_mixer_selem_has_capture_switch (element)) {
      if (!(mixer->dir & GST_ALSA_MIXER_CAPTURE))
        goto next;
      flags = GST_MIXER_TRACK_INPUT;
    } else {
      if (!(mixer->dir & GST_ALSA_MIXER_PLAYBACK))
        goto next;
    }

    /* prevent dup names */
    for (item = mixer->tracklist; item != NULL; item = item->next) {
      snd_mixer_elem_t *temp;

      if (GST_IS_ALSA_MIXER_OPTIONS (item->data))
        temp = GST_ALSA_MIXER_OPTIONS (item->data)->element;
      else
        temp = GST_ALSA_MIXER_TRACK (item->data)->element;

      if (!strcmp (snd_mixer_selem_get_name (element),
              snd_mixer_selem_get_name (temp)))
        samename++;
    }

    if (snd_mixer_selem_has_capture_volume (element)) {
      while (snd_mixer_selem_has_capture_channel (element, channels))
        channels++;
      track = gst_alsa_mixer_track_new (element, samename,
          i, channels, flags, GST_ALSA_MIXER_TRACK_CAPTURE);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
      got_it = TRUE;

      /* there might be another volume slider; make that playback */
      flags &= ~GST_MIXER_TRACK_INPUT;
      flags |= GST_MIXER_TRACK_OUTPUT;
    }

    if (snd_mixer_selem_has_playback_volume (element)) {
      while (snd_mixer_selem_has_playback_channel (element, channels))
        channels++;
      if (first) {
        first = FALSE;
        flags |= GST_MIXER_TRACK_MASTER;
      }
      track = gst_alsa_mixer_track_new (element, samename,
          i, channels, flags, GST_ALSA_MIXER_TRACK_PLAYBACK);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
      got_it = TRUE;
    }

    if (snd_mixer_selem_is_enumerated (element)) {
      opts = gst_alsa_mixer_options_new (element, i);
      mixer->tracklist = g_list_append (mixer->tracklist, opts);
      got_it = TRUE;
    }

    if (!got_it) {
      if (flags == GST_MIXER_TRACK_OUTPUT &&
          snd_mixer_selem_has_playback_switch (element)) {
        /* simple mute switch */
        track = gst_alsa_mixer_track_new (element, samename,
            i, 0, flags, GST_ALSA_MIXER_TRACK_PLAYBACK);
        mixer->tracklist = g_list_append (mixer->tracklist, track);
      }
    }

  next:
    element = snd_mixer_elem_next (element);
  }
}


/* API */

GstAlsaMixer *
gst_alsa_mixer_new (const char *device, GstAlsaMixerDirection dir)
{
  GstAlsaMixer *ret = NULL;

  g_return_val_if_fail (device != NULL, NULL);

  ret = g_new0 (GstAlsaMixer, 1);

  ret->device = g_strdup (device);
  ret->dir = dir;

  if (!gst_alsa_mixer_open (ret))
    goto error;

  return ret;

error:
  if (ret)
    gst_alsa_mixer_free (ret);

  return NULL;
}

void
gst_alsa_mixer_free (GstAlsaMixer * mixer)
{
  g_return_if_fail (mixer != NULL);

  if (mixer->device) {
    g_free (mixer->device);
    mixer->device = NULL;
  }

  if (mixer->cardname) {
    g_free (mixer->cardname);
    mixer->cardname = NULL;
  }

  if (mixer->tracklist) {
    g_list_foreach (mixer->tracklist, (GFunc) g_object_unref, NULL);
    g_list_free (mixer->tracklist);
    mixer->tracklist = NULL;
  }

  if (mixer->handle) {
    snd_mixer_close (mixer->handle);
    mixer->handle = NULL;
  }

  g_free (mixer);
}

const GList *
gst_alsa_mixer_list_tracks (GstAlsaMixer * mixer)
{
  g_return_val_if_fail (mixer->handle != NULL, NULL);

  gst_alsa_mixer_ensure_track_list (mixer);

  return (const GList *) mixer->tracklist;
}

static void
gst_alsa_mixer_update (GstAlsaMixer * mixer, GstAlsaMixerTrack * alsa_track)
{
  GstMixerTrack *track = (GstMixerTrack *) alsa_track;
  int v = 0;

  snd_mixer_handle_events (mixer->handle);
  if (!alsa_track)
    return;

  /* Any updates in flags? */
  if (snd_mixer_selem_has_playback_switch (alsa_track->element)) {
    snd_mixer_selem_get_playback_switch (alsa_track->element, 0, &v);
    if (v)
      track->flags &= ~GST_MIXER_TRACK_MUTE;
    else
      track->flags |= GST_MIXER_TRACK_MUTE;
  }
  if (alsa_track->alsa_flags & GST_ALSA_MIXER_TRACK_CAPTURE) {
    snd_mixer_selem_get_capture_switch (alsa_track->element, 0, &v);
    if (!v)
      track->flags &= ~GST_MIXER_TRACK_RECORD;
    else
      track->flags |= GST_MIXER_TRACK_RECORD;
  }
}

void
gst_alsa_mixer_get_volume (GstAlsaMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  gint i;
  GstAlsaMixerTrack *alsa_track = GST_ALSA_MIXER_TRACK (track);

  g_return_if_fail (mixer->handle != NULL);

  gst_alsa_mixer_update (mixer, alsa_track);

  if (track->flags & GST_MIXER_TRACK_MUTE &&
      !snd_mixer_selem_has_playback_switch (alsa_track->element)) {
    for (i = 0; i < track->num_channels; i++)
      volumes[i] = alsa_track->volumes[i];
  } else {
    for (i = 0; i < track->num_channels; i++) {
      long tmp = 0;

      if (alsa_track->alsa_flags & GST_ALSA_MIXER_TRACK_PLAYBACK) {
        snd_mixer_selem_get_playback_volume (alsa_track->element, i, &tmp);
      } else if (alsa_track->alsa_flags & GST_ALSA_MIXER_TRACK_CAPTURE) {
        snd_mixer_selem_get_capture_volume (alsa_track->element, i, &tmp);
      }

      alsa_track->volumes[i] = volumes[i] = (gint) tmp;
    }
  }
}

void
gst_alsa_mixer_set_volume (GstAlsaMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  gint i;
  GstAlsaMixerTrack *alsa_track = GST_ALSA_MIXER_TRACK (track);

  g_return_if_fail (mixer->handle != NULL);

  gst_alsa_mixer_update (mixer, alsa_track);

  /* only set the volume with ALSA lib if the track isn't muted. */
  for (i = 0; i < track->num_channels; i++) {
    alsa_track->volumes[i] = volumes[i];

    if (!(track->flags & GST_MIXER_TRACK_MUTE) ||
        snd_mixer_selem_has_playback_switch (alsa_track->element)) {
      if (alsa_track->alsa_flags & GST_ALSA_MIXER_TRACK_PLAYBACK) {
        snd_mixer_selem_set_playback_volume (alsa_track->element, i,
            (long) volumes[i]);
      } else if (alsa_track->alsa_flags & GST_ALSA_MIXER_TRACK_CAPTURE) {
        snd_mixer_selem_set_capture_volume (alsa_track->element, i,
            (long) volumes[i]);
      }
    }
  }
}

void
gst_alsa_mixer_set_mute (GstAlsaMixer * mixer, GstMixerTrack * track,
    gboolean mute)
{
  gint i;
  GstAlsaMixerTrack *alsa_track = GST_ALSA_MIXER_TRACK (track);

  g_return_if_fail (mixer->handle != NULL);

  gst_alsa_mixer_update (mixer, alsa_track);

  if (mute) {
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }

  if (snd_mixer_selem_has_playback_switch (alsa_track->element)) {
    snd_mixer_selem_set_playback_switch_all (alsa_track->element, mute ? 0 : 1);
  } else {
    for (i = 0; i < track->num_channels; i++) {
      long vol = mute ? 0 : alsa_track->volumes[i];

      if (alsa_track->alsa_flags & GST_ALSA_MIXER_TRACK_CAPTURE) {
        snd_mixer_selem_set_capture_volume (alsa_track->element, i, vol);
      } else if (alsa_track->alsa_flags & GST_ALSA_MIXER_TRACK_PLAYBACK) {
        snd_mixer_selem_set_playback_volume (alsa_track->element, i, vol);
      }
    }
  }
}

void
gst_alsa_mixer_set_record (GstAlsaMixer * mixer,
    GstMixerTrack * track, gboolean record)
{
  GstAlsaMixerTrack *alsa_track = GST_ALSA_MIXER_TRACK (track);

  g_return_if_fail (mixer->handle != NULL);

  gst_alsa_mixer_update (mixer, alsa_track);

  if (record) {
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }

  snd_mixer_selem_set_capture_switch_all (alsa_track->element, record ? 1 : 0);
}

void
gst_alsa_mixer_set_option (GstAlsaMixer * mixer,
    GstMixerOptions * opts, gchar * value)
{
  gint idx = -1, n = 0;
  GList *item;
  GstAlsaMixerOptions *alsa_opts = GST_ALSA_MIXER_OPTIONS (opts);

  g_return_if_fail (mixer->handle != NULL);

  gst_alsa_mixer_update (mixer, NULL);

  for (item = opts->values; item != NULL; item = item->next, n++) {
    if (!strcmp (item->data, value)) {
      idx = n;
      break;
    }
  }
  if (idx == -1)
    return;

  snd_mixer_selem_set_enum_item (alsa_opts->element, 0, idx);
}

const gchar *
gst_alsa_mixer_get_option (GstAlsaMixer * mixer, GstMixerOptions * opts)
{
  gint ret;
  guint idx;
  GstAlsaMixerOptions *alsa_opts = GST_ALSA_MIXER_OPTIONS (opts);

  g_return_val_if_fail (mixer->handle != NULL, NULL);

  gst_alsa_mixer_update (mixer, NULL);

  ret = snd_mixer_selem_get_enum_item (alsa_opts->element, 0, &idx);
  if (ret == 0)
    return g_list_nth_data (opts->values, idx);
  else
    return snd_strerror (ret);  /* feeble attempt at error handling */
}
