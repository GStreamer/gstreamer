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
static GstElementDetails gst_alsa_mixer_details = GST_ELEMENT_DETAILS (
  "Alsa Mixer",
  "Generic/Audio",
  "Control sound input and output levels with ALSA",
  "Leif Johnson <leif@ambient.2y.net>"
);

static void			gst_alsa_interface_init		(GstImplementsInterfaceClass *klass);

static void			gst_alsa_mixer_class_init	(gpointer       g_class,
                                                                 gpointer       class_data);
static void			gst_alsa_mixer_init		(GstAlsaMixer *	mixer);
static void			gst_alsa_mixer_dispose		(GObject *	object);
static void			gst_alsa_mixer_interface_init	(GstMixerClass*	klass);
static gboolean			gst_alsa_mixer_supported	(GstImplementsInterface *	iface,
								 GType		iface_type);

/* GStreamer stuff */
static GstElementStateReturn	gst_alsa_mixer_change_state	(GstElement *	element);

static void			gst_alsa_mixer_build_list	(GstAlsaMixer *	mixer);
static void			gst_alsa_mixer_free_list	(GstAlsaMixer *	mixer);

/* interface implementation */
static const GList *		gst_alsa_mixer_list_tracks	(GstMixer *	mixer);

static void			gst_alsa_mixer_set_volume	(GstMixer *	mixer,
								 GstMixerTrack*	track,
								 gint *		volumes);
static void			gst_alsa_mixer_get_volume	(GstMixer *	mixer,
								 GstMixerTrack*	track,
								 gint *		volumes);

static void			gst_alsa_mixer_set_record	(GstMixer *	mixer,
								 GstMixerTrack*	track,
								 gboolean	record);
static void			gst_alsa_mixer_set_mute		(GstMixer *	mixer,
								 GstMixerTrack*	track,
								 gboolean	mute);

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

    alsa_mixer_type = g_type_register_static (GST_TYPE_ALSA, "GstAlsaMixer", &alsa_mixer_info, 0);

    g_type_add_interface_static (alsa_mixer_type, GST_TYPE_IMPLEMENTS_INTERFACE, &alsa_iface_info);
    g_type_add_interface_static (alsa_mixer_type, GST_TYPE_MIXER, &alsa_mixer_iface_info);
  }

  return alsa_mixer_type;
}

static void
gst_alsa_mixer_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class;
  GstElementClass *element_class;
  GstAlsaClass *klass;

  klass = (GstAlsaClass *)g_class;
  object_class = (GObjectClass *) g_class;
  element_class = (GstElementClass *) g_class;

  if (parent_class == NULL)
    parent_class = g_type_class_ref (GST_TYPE_ALSA);

  object_class->dispose = gst_alsa_mixer_dispose;

  element_class->change_state = gst_alsa_mixer_change_state;

  gst_element_class_set_details (element_class, &gst_alsa_mixer_details);
}

static void
gst_alsa_mixer_init (GstAlsaMixer * mixer)
{
  gint err;
  GstAlsa *alsa = GST_ALSA (mixer);

  mixer->mixer_handle = (snd_mixer_t *) -1;

  /* open and initialize the mixer device */
  err = snd_mixer_open(&mixer->mixer_handle, 0);
  if (err < 0 || mixer->mixer_handle == NULL) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer), "Cannot open mixer device.");
    mixer->mixer_handle = (snd_mixer_t *) -1;
    return;
  }

  if ((err = snd_mixer_attach(mixer->mixer_handle, alsa->device)) < 0) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer),
                      "Cannot attach mixer to sound device `%s'.",
                      alsa->device);
    goto error;
  }

  if ((err = snd_mixer_selem_register(mixer->mixer_handle, NULL, NULL)) < 0) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer), "Cannot register mixer elements.");
    goto error;
  }

  if ((err = snd_mixer_load(mixer->mixer_handle)) < 0) {
    GST_ERROR_OBJECT (GST_OBJECT (mixer), "Cannot load mixer settings.");
    goto error;
  }

  return;

 error:
  snd_mixer_close (mixer->mixer_handle);
  mixer->mixer_handle = (snd_mixer_t *) -1;
}

static void
gst_alsa_mixer_dispose (GObject * object)
{
  GstAlsaMixer *mixer = GST_ALSA_MIXER (object);

  if (((gint) mixer->mixer_handle) == -1) return;

  snd_mixer_close (mixer->mixer_handle);
  mixer->mixer_handle = (snd_mixer_t *) -1;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_alsa_interface_init (GstImplementsInterfaceClass *klass)
{
  klass->supported = gst_alsa_mixer_supported;
}

static void
gst_alsa_mixer_interface_init (GstMixerClass *klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;
  
  /* set up the interface hooks */
  klass->list_tracks = gst_alsa_mixer_list_tracks;
  klass->set_volume = gst_alsa_mixer_set_volume;
  klass->get_volume = gst_alsa_mixer_get_volume;
  klass->set_mute = gst_alsa_mixer_set_mute;
  klass->set_record = gst_alsa_mixer_set_record;
}

gboolean
gst_alsa_mixer_supported (GstImplementsInterface *iface, GType iface_type)
{
  g_assert (iface_type == GST_TYPE_MIXER);

  return (((gint) GST_ALSA_MIXER (iface)->mixer_handle) != -1);
}

static void
gst_alsa_mixer_build_list (GstAlsaMixer *mixer)
{
  gint i, count;
  snd_mixer_elem_t *element;

  g_return_if_fail (((gint) mixer->mixer_handle) != -1);

  count = snd_mixer_get_count(mixer->mixer_handle);
  element = snd_mixer_first_elem(mixer->mixer_handle);

  /* build track list */

  for (i = 0; i < count; i++) {
    gint channels = 0;

    if (! snd_mixer_selem_is_active(element)) continue;

    /* find out if this element can be an input */

    if (snd_mixer_selem_has_capture_channel(element, 0) ||
        snd_mixer_selem_has_capture_switch(element) ||
        snd_mixer_selem_is_capture_mono(element)) {
      while (snd_mixer_selem_has_capture_channel(element, channels))
        channels++;

      GstMixerTrack *track = gst_alsa_mixer_track_new
        (element, i, channels, GST_MIXER_TRACK_INPUT);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }

    /* find out if this element can also be an output */

    channels = 0;

    if (snd_mixer_selem_has_playback_channel(element, 0) ||
        snd_mixer_selem_has_playback_switch(element) ||
        snd_mixer_selem_is_playback_mono(element)) {
      while (snd_mixer_selem_has_playback_channel(element, channels))
        channels++;

      GstMixerTrack *track = gst_alsa_mixer_track_new
        (element, i, channels, GST_MIXER_TRACK_OUTPUT);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }

    element = snd_mixer_elem_next(element);
  }
}

static void
gst_alsa_mixer_free_list (GstAlsaMixer *mixer)
{
  g_return_if_fail (((gint) mixer->mixer_handle) != -1);

  g_list_foreach (mixer->tracklist, (GFunc) gst_alsa_mixer_track_free, NULL);
  g_list_free (mixer->tracklist);
  mixer->tracklist = NULL;
}

/*** GSTREAMER FUNCTIONS ******************************************************/

static GstElementStateReturn
gst_alsa_mixer_change_state (GstElement *element)
{
  GstAlsaMixer *this;

  g_return_val_if_fail (element != NULL, FALSE);
  this = GST_ALSA_MIXER (element);

  switch (GST_STATE_TRANSITION (element)) {
  case GST_STATE_NULL_TO_READY:
    gst_alsa_mixer_build_list (this); break;
  case GST_STATE_READY_TO_NULL:
    gst_alsa_mixer_free_list (this); break;
  default:
    g_assert_not_reached();
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*** INTERFACE IMPLEMENTATION *************************************************/

static const GList *
gst_alsa_mixer_list_tracks (GstMixer *mixer)
{
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);

  g_return_val_if_fail (((gint) alsa_mixer->mixer_handle) != -1, NULL);

  return (const GList *) alsa_mixer->tracklist;
}

static void
gst_alsa_mixer_get_volume (GstMixer *mixer,
                           GstMixerTrack *track,
                           gint *volumes)
{
  gint i;
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (((gint) alsa_mixer->mixer_handle) != -1);

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
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (((gint) alsa_mixer->mixer_handle) != -1);

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
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);
  GstAlsaMixerTrack *alsa_track = (GstAlsaMixerTrack *) track;

  g_return_if_fail (((gint) alsa_mixer->mixer_handle) != -1);

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
  GstAlsaMixer *alsa_mixer = GST_ALSA_MIXER (mixer);

  g_return_if_fail (((gint) alsa_mixer->mixer_handle) != -1);

  if (record) {
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }
}
