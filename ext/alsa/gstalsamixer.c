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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalsamixer.h"

/* elementfactory information */
static GstElementDetails gst_alsa_mixer_details =
GST_ELEMENT_DETAILS ("Alsa Mixer",
    "Generic/Audio",
    "Control sound input and output levels with ALSA",
    "Leif Johnson <leif@ambient.2y.net>");

static void gst_alsa_interface_init (GstImplementsInterfaceClass * klass);

static void gst_alsa_mixer_class_init (gpointer g_class, gpointer class_data);
static void gst_alsa_mixer_init (GstAlsaMixer * mixer);
static void gst_alsa_mixer_interface_init (GstMixerClass * klass);
static gboolean gst_alsa_mixer_supported (GstImplementsInterface * iface,
    GType iface_type);

/* GStreamer stuff */
static GstElementStateReturn gst_alsa_mixer_change_state (GstElement * element);

static void gst_alsa_mixer_build_list (GstAlsaMixer * mixer);
static void gst_alsa_mixer_free_list (GstAlsaMixer * mixer);

/* interface implementation */
static const GList *gst_alsa_mixer_list_tracks (GstMixer * mixer);

static void gst_alsa_mixer_set_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes);
static void gst_alsa_mixer_get_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes);

static void gst_alsa_mixer_set_record (GstMixer * mixer,
    GstMixerTrack * track, gboolean record);
static void gst_alsa_mixer_set_mute (GstMixer * mixer,
    GstMixerTrack * track, gboolean mute);

static void gst_alsa_mixer_set_option (GstMixer * mixer,
    GstMixerOptions * opts, gchar * value);
static const gchar *gst_alsa_mixer_get_option (GstMixer * mixer,
    GstMixerOptions * opts);

/*** GOBJECT STUFF ************************************************************/

static GstAlsa *parent_class = NULL;

GType
gst_alsa_mixer_get_type (void)
{
  static GType alsa_mixer_type = 0;

  if (!alsa_mixer_type) {
    static const GTypeInfo alsa_mixer_info = {
      sizeof (GstAlsaMixerClass),
      NULL,
      NULL,
      gst_alsa_mixer_class_init,
      NULL,
      NULL,
      sizeof (GstAlsaMixer),
      0,
      (GInstanceInitFunc) gst_alsa_mixer_init,
    };
    static const GInterfaceInfo alsa_iface_info = {
      (GInterfaceInitFunc) gst_alsa_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo alsa_mixer_iface_info = {
      (GInterfaceInitFunc) gst_alsa_mixer_interface_init,
      NULL,
      NULL,
    };

    alsa_mixer_type =
        g_type_register_static (GST_TYPE_ALSA, "GstAlsaMixer", &alsa_mixer_info,
        0);

    g_type_add_interface_static (alsa_mixer_type, GST_TYPE_IMPLEMENTS_INTERFACE,
        &alsa_iface_info);
    g_type_add_interface_static (alsa_mixer_type, GST_TYPE_MIXER,
        &alsa_mixer_iface_info);
  }

  return alsa_mixer_type;
}

static void
gst_alsa_mixer_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class;
  GstElementClass *element_class;
  GstAlsaClass *klass;

  klass = (GstAlsaClass *) g_class;
  object_class = (GObjectClass *) g_class;
  element_class = (GstElementClass *) g_class;

  if (parent_class == NULL)
    parent_class = g_type_class_ref (GST_TYPE_ALSA);

  element_class->change_state = gst_alsa_mixer_change_state;

  gst_element_class_set_details (element_class, &gst_alsa_mixer_details);
}

static void
gst_alsa_mixer_init (GstAlsaMixer * mixer)
{
  mixer->mixer_handle = NULL;
}

static gboolean
gst_alsa_mixer_open (GstAlsaMixer * mixer)
{
  gint err, device;
  GstAlsa *alsa = GST_ALSA (mixer);
  gchar *nocomma = NULL;

  mixer->mixer_handle = NULL;

  /* open and initialize the mixer device */
  err = snd_mixer_open (&mixer->mixer_handle, 0);
  if (err < 0 || mixer->mixer_handle == NULL) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer), "Cannot open mixer device.");
    mixer->mixer_handle = NULL;
    return FALSE;
  }

  if (!strncmp (alsa->device, "hw:", 3))
    nocomma = g_strdup (alsa->device);
  else if (!strncmp (alsa->device, "plughw:", 7))
    nocomma = g_strdup (alsa->device + 4);
  else
    goto error;

  if (strchr (nocomma, ','))
    strchr (nocomma, ',')[0] = '\0';

  if ((err = snd_mixer_attach (mixer->mixer_handle, nocomma)) < 0) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer),
        "Cannot attach mixer to sound device `%s'.", nocomma);
    goto error;
  }

  if ((err = snd_mixer_selem_register (mixer->mixer_handle, NULL, NULL)) < 0) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer), "Cannot register mixer elements.");
    goto error;
  }

  if ((err = snd_mixer_load (mixer->mixer_handle)) < 0) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer), "Cannot load mixer settings.");
    goto error;
  }

  /* I don't know how to get a device name from a mixer handle. So on
   * to the ugly hacks here, then... */
  if (sscanf (nocomma, "hw:%d", &device) == 1) {
    gchar *name;

    if (!snd_card_get_name (device, &name))
      alsa->cardname = name;
  }

  g_free (nocomma);

  return TRUE;

error:
  snd_mixer_close (mixer->mixer_handle);
  mixer->mixer_handle = NULL;
  g_free (nocomma);
  return FALSE;
}

static void
gst_alsa_mixer_close (GstAlsaMixer * mixer)
{
  GstAlsa *alsa = GST_ALSA (mixer);

  if (mixer->mixer_handle == NULL)
    return;

  if (alsa->cardname) {
    g_free (alsa->cardname);
    alsa->cardname = NULL;
  }
  snd_mixer_close (mixer->mixer_handle);
  mixer->mixer_handle = NULL;
}

static void
gst_alsa_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_alsa_mixer_supported;
}

static void
gst_alsa_mixer_interface_init (GstMixerClass * klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;

  /* set up the interface hooks */
  klass->list_tracks = gst_alsa_mixer_list_tracks;
  klass->set_volume = gst_alsa_mixer_set_volume;
  klass->get_volume = gst_alsa_mixer_get_volume;
  klass->set_mute = gst_alsa_mixer_set_mute;
  klass->set_record = gst_alsa_mixer_set_record;
  klass->set_option = gst_alsa_mixer_set_option;
  klass->get_option = gst_alsa_mixer_get_option;
}

gboolean
gst_alsa_mixer_supported (GstImplementsInterface * iface, GType iface_type)
{
  g_assert (iface_type == GST_TYPE_MIXER);

  return (GST_ALSA_MIXER (iface)->mixer_handle != NULL);
}

static void
gst_alsa_mixer_build_list (GstAlsaMixer * mixer)
{
  gint i, count;
  snd_mixer_elem_t *element;
  GstMixerTrack *track;
  GstMixerOptions *opts;
  const GList *templates;
  GstPadDirection dir = GST_PAD_UNKNOWN;

  g_return_if_fail (mixer->mixer_handle != NULL);

  /* find direction */
  templates =
      gst_element_class_get_pad_template_list (GST_ELEMENT_GET_CLASS (mixer));
  if (templates)
    dir = GST_PAD_TEMPLATE (templates->data)->direction;

  count = snd_mixer_get_count (mixer->mixer_handle);
  element = snd_mixer_first_elem (mixer->mixer_handle);

  /* build track list */
  for (i = 0; i < count; i++) {
    gint channels = 0;
    gint flags = GST_MIXER_TRACK_OUTPUT;

    if (snd_mixer_selem_has_capture_switch (element)) {
      if (dir != GST_PAD_SRC && dir != GST_PAD_UNKNOWN)
        continue;
      flags = GST_MIXER_TRACK_INPUT;
    } else {
      if (dir != GST_PAD_SINK && dir != GST_PAD_UNKNOWN)
        continue;
    }

    if (snd_mixer_selem_has_capture_volume (element)) {
      while (snd_mixer_selem_has_capture_channel (element, channels))
        channels++;
      track = gst_alsa_mixer_track_new (element, i, channels,
          flags, GST_ALSA_MIXER_TRACK_CAPTURE);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }

    if (snd_mixer_selem_has_playback_volume (element)) {
      while (snd_mixer_selem_has_playback_channel (element, channels))
        channels++;
      track = gst_alsa_mixer_track_new (element, i, channels,
          flags, GST_ALSA_MIXER_TRACK_PLAYBACK);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }

    if (snd_mixer_selem_is_enumerated (element)) {
      opts = gst_alsa_mixer_options_new (element, i);
      mixer->tracklist = g_list_append (mixer->tracklist, opts);
    }

    element = snd_mixer_elem_next (element);
  }
}

static void
gst_alsa_mixer_free_list (GstAlsaMixer * mixer)
{
  g_return_if_fail (mixer->mixer_handle != NULL);

  g_list_foreach (mixer->tracklist, (GFunc) g_object_unref, NULL);
  g_list_free (mixer->tracklist);
  mixer->tracklist = NULL;
}

/*** GSTREAMER FUNCTIONS ******************************************************/

static GstElementStateReturn
gst_alsa_mixer_change_state (GstElement * element)
{
  GstAlsaMixer *this;

  g_return_val_if_fail (element != NULL, FALSE);
  this = GST_ALSA_MIXER (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (gst_alsa_mixer_open (this))
        gst_alsa_mixer_build_list (this);
      break;
    case GST_STATE_READY_TO_NULL:
      if (this->mixer_handle != NULL) {
        gst_alsa_mixer_free_list (this);
        gst_alsa_mixer_close (this);
      }
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*** INTERFACE IMPLEMENTATION *************************************************/

static const GList *
gst_alsa_mixer_list_tracks (GstMixer * mixer)
{
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);

  if (!alsa_mixer->mixer_handle)
    return NULL;

  return (const GList *) alsa_mixer->tracklist;
}

static void
gst_alsa_mixer_get_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint i;
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (alsa_mixer->mixer_handle != NULL);

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
      volumes[i] = (gint) tmp;
    }
  }
}

static void
gst_alsa_mixer_set_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint i;
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (alsa_mixer->mixer_handle != NULL);

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

static void
gst_alsa_mixer_set_mute (GstMixer * mixer, GstMixerTrack * track, gboolean mute)
{
  gint i;
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (alsa_mixer->mixer_handle != NULL);

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

static void
gst_alsa_mixer_set_record (GstMixer * mixer,
    GstMixerTrack * track, gboolean record)
{
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (alsa_mixer->mixer_handle != NULL);

  if (record) {
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }

  snd_mixer_selem_set_capture_switch_all (alsa_track->element, record ? 1 : 0);
}

static void
gst_alsa_mixer_set_option (GstMixer * mixer,
    GstMixerOptions * opts, gchar * value)
{
  gint idx = -1, n = 0;
  GList *item;
  GstAlsaMixer *alsa_mixer = (GstAlsaMixer *) mixer;
  GstAlsaMixerOptions *alsa_opts = (GstAlsaMixerOptions *) opts;

  g_return_if_fail (alsa_mixer->mixer_handle != NULL);

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

static const gchar *
gst_alsa_mixer_get_option (GstMixer * mixer, GstMixerOptions * opts)
{
  GstAlsaMixer *alsa_mixer = (GstAlsaMixer *) mixer;
  GstAlsaMixerOptions *alsa_opts = (GstAlsaMixerOptions *) opts;
  gint idx = -1;

  g_return_val_if_fail (alsa_mixer->mixer_handle != NULL, NULL);

  snd_mixer_selem_get_enum_item (alsa_opts->element, 0, &idx);

  return g_list_nth_data (opts->values, idx);
}
