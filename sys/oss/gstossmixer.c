/* GStreamer OSS Mixer implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstossmixer.c: mixer interface implementation for OSS
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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#ifdef HAVE_OSS_INCLUDE_IN_SYS
#include <sys/soundcard.h>
#else

#ifdef HAVE_OSS_INCLUDE_IN_ROOT
#include <soundcard.h>
#else

#include <machine/soundcard.h>

#endif /* HAVE_OSS_INCLUDE_IN_ROOT */

#endif /* HAVE_OSS_INCLUDE_IN_SYS */

#include <gst/gst-i18n-plugin.h>

#include "gstossmixer.h"

#define MASK_BIT_IS_SET(mask, bit) \
  (mask & (1 << bit))

static void gst_ossmixer_track_class_init (GstOssMixerTrackClass * klass);
static void gst_ossmixer_track_init (GstOssMixerTrack * track);

static gboolean gst_ossmixer_supported (GstImplementsInterface * iface,
    GType iface_type);
static const GList *gst_ossmixer_list_tracks (GstMixer * ossmixer);

static void gst_ossmixer_set_volume (GstMixer * ossmixer,
    GstMixerTrack * track, gint * volumes);
static void gst_ossmixer_get_volume (GstMixer * ossmixer,
    GstMixerTrack * track, gint * volumes);

static void gst_ossmixer_set_record (GstMixer * ossmixer,
    GstMixerTrack * track, gboolean record);
static void gst_ossmixer_set_mute (GstMixer * ossmixer,
    GstMixerTrack * track, gboolean mute);

static const gchar **labels = NULL;
static GstMixerTrackClass *parent_class = NULL;

/* three functions: firstly, OSS has the nasty habit of inserting
 * spaces in the labels, we want to get rid of them. Secondly,
 * i18n is impossible with OSS' way of providing us with mixer
 * labels, so we make a 'given' list of i18n'ed labels. Thirdly, I
 * personally don't like the "1337" names that OSS gives to their
 * labels ("Vol", "Mic", "Rec"), I'd rather see full names. */

static void
fill_labels (void)
{
  gint i, pos;
  gchar *origs[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_LABELS;
  struct
  {
    gchar *given, *wanted;
  }
  cases[] =
  {
    /* Note: this list is simply ripped from soundcard.h. For
     * some people, some values might be missing (3D surround,
     * etc.) - feel free to add them. That's the reason why
     * I'm doing this in such a horribly complicated way. */
    {
    "Vol  ", _("Volume")}
    , {
    "Bass ", _("Bass")}
    , {
    "Trebl", _("Treble")}
    , {
    "Synth", _("Synth")}
    , {
    "Pcm  ", _("PCM")}
    , {
    "Spkr ", _("Speaker")}
    , {
    "Line ", _("Line-in")}
    , {
    "Mic  ", _("Microphone")}
    , {
    "CD   ", _("CD")}
    , {
    "Mix  ", _("Mixer")}
    , {
    "Pcm2 ", _("PCM-2")}
    , {
    "Rec  ", _("Record")}
    , {
    "IGain", _("In-gain")}
    , {
    "OGain", _("Out-gain")}
    , {
    "Line1", _("Line-1")}
    , {
    "Line2", _("Line-2")}
    , {
    "Line3", _("Line-3")}
    , {
    "Digital1", _("Digital-1")}
    , {
    "Digital2", _("Digital-2")}
    , {
    "Digital3", _("Digital-3")}
    , {
    "PhoneIn", _("Phone-in")}
    , {
    "PhoneOut", _("Phone-out")}
    , {
    "Video", _("Video")}
    , {
    "Radio", _("Radio")}
    , {
    "Monitor", _("Monitor")}
    , {
    NULL, NULL}
  };

  labels = g_malloc (sizeof (gchar *) * SOUND_MIXER_NRDEVICES);

  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
    for (pos = 0; cases[pos].given != NULL; pos++) {
      if (!strcmp (cases[pos].given, origs[i])) {
        labels[i] = g_strdup (cases[pos].wanted);
        break;
      }
    }
    if (cases[pos].given == NULL)
      labels[i] = g_strdup (origs[i]);
  }
}

GType
gst_ossmixer_track_get_type (void)
{
  static GType gst_ossmixer_track_type = 0;

  if (!gst_ossmixer_track_type) {
    static const GTypeInfo ossmixer_track_info = {
      sizeof (GstOssMixerTrackClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ossmixer_track_class_init,
      NULL,
      NULL,
      sizeof (GstOssMixerTrack),
      0,
      (GInstanceInitFunc) gst_ossmixer_track_init,
      NULL
    };

    gst_ossmixer_track_type =
        g_type_register_static (GST_TYPE_MIXER_TRACK,
        "GstOssMixerTrack", &ossmixer_track_info, 0);
  }

  return gst_ossmixer_track_type;
}

static void
gst_ossmixer_track_class_init (GstOssMixerTrackClass * klass)
{
  parent_class = g_type_class_ref (GST_TYPE_MIXER_TRACK);
}

static void
gst_ossmixer_track_init (GstOssMixerTrack * track)
{
  track->lvol = track->rvol = 0;
  track->track_num = 0;
}

GstMixerTrack *
gst_ossmixer_track_new (GstOssElement * oss,
    gint track_num, gint max_chans, gint flags)
{
  GstOssMixerTrack *osstrack;
  GstMixerTrack *track;
  gint volume;

  if (!labels)
    fill_labels ();

  osstrack = g_object_new (GST_TYPE_OSSMIXER_TRACK, NULL);
  track = GST_MIXER_TRACK (osstrack);
  track->label = g_strdup (labels[track_num]);
  track->num_channels = max_chans;
  track->flags = flags;
  track->min_volume = 0;
  track->max_volume = 100;
  osstrack->track_num = track_num;

  /* volume */
  if (ioctl (oss->mixer_fd, MIXER_READ (osstrack->track_num), &volume) < 0) {
    g_warning ("Error getting device (%d) volume: %s",
        osstrack->track_num, strerror (errno));
    volume = 0;
  }
  osstrack->lvol = (volume & 0xff);
  if (track->num_channels == 2) {
    osstrack->rvol = ((volume >> 8) & 0xff);
  }

  return track;
}

void
gst_oss_interface_init (GstImplementsInterfaceClass * klass)
{
  /* default virtual functions */
  klass->supported = gst_ossmixer_supported;
}

void
gst_ossmixer_interface_init (GstMixerClass * klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;

  /* default virtual functions */
  klass->list_tracks = gst_ossmixer_list_tracks;
  klass->set_volume = gst_ossmixer_set_volume;
  klass->get_volume = gst_ossmixer_get_volume;
  klass->set_mute = gst_ossmixer_set_mute;
  klass->set_record = gst_ossmixer_set_record;
}

static gboolean
gst_ossmixer_supported (GstImplementsInterface * iface, GType iface_type)
{
  g_assert (iface_type == GST_TYPE_MIXER);

  return (GST_OSSELEMENT (iface)->mixer_fd != -1);
}

/* unused with G_DISABLE_* */
static G_GNUC_UNUSED gboolean
gst_ossmixer_contains_track (GstOssElement * oss, GstOssMixerTrack * osstrack)
{
  const GList *item;

  for (item = oss->tracklist; item != NULL; item = item->next)
    if (item->data == osstrack)
      return TRUE;

  return FALSE;
}

static const GList *
gst_ossmixer_list_tracks (GstMixer * mixer)
{
  return (const GList *) GST_OSSELEMENT (mixer)->tracklist;
}

static void
gst_ossmixer_get_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (oss->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (oss, osstrack));

  if (track->flags & GST_MIXER_TRACK_MUTE) {
    volumes[0] = osstrack->lvol;
    if (track->num_channels == 2) {
      volumes[1] = osstrack->rvol;
    }
  } else {
    /* get */
    if (ioctl (oss->mixer_fd, MIXER_READ (osstrack->track_num), &volume) < 0) {
      g_warning ("Error getting recording device (%d) volume: %s",
          osstrack->track_num, strerror (errno));
      volume = 0;
    }

    osstrack->lvol = volumes[0] = (volume & 0xff);
    if (track->num_channels == 2) {
      osstrack->rvol = volumes[1] = ((volume >> 8) & 0xff);
    }
  }
}

static void
gst_ossmixer_set_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (oss->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (oss, osstrack));

  /* prepare the value for ioctl() */
  if (!(track->flags & GST_MIXER_TRACK_MUTE)) {
    volume = (volumes[0] & 0xff);
    if (track->num_channels == 2) {
      volume |= ((volumes[1] & 0xff) << 8);
    }

    /* set */
    if (ioctl (oss->mixer_fd, MIXER_WRITE (osstrack->track_num), &volume) < 0) {
      g_warning ("Error setting recording device (%d) volume (0x%x): %s",
          osstrack->track_num, volume, strerror (errno));
      return;
    }
  }

  osstrack->lvol = volumes[0];
  if (track->num_channels == 2) {
    osstrack->rvol = volumes[1];
  }
}

static void
gst_ossmixer_set_mute (GstMixer * mixer, GstMixerTrack * track, gboolean mute)
{
  int volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (oss->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (oss, osstrack));

  if (mute) {
    volume = 0;
  } else {
    volume = (osstrack->lvol & 0xff);
    if (MASK_BIT_IS_SET (oss->stereomask, osstrack->track_num)) {
      volume |= ((osstrack->rvol & 0xff) << 8);
    }
  }

  if (ioctl (oss->mixer_fd, MIXER_WRITE (osstrack->track_num), &volume) < 0) {
    g_warning ("Error setting mixer recording device volume (0x%x): %s",
        volume, strerror (errno));
    return;
  }

  if (mute) {
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }
}

static void
gst_ossmixer_set_record (GstMixer * mixer,
    GstMixerTrack * track, gboolean record)
{
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (oss->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (oss, osstrack));

  /* if there's nothing to do... */
  if ((record && GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)) ||
      (!record && !GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)))
    return;

  /* if we're exclusive, then we need to unset the current one(s) */
  if (oss->mixcaps & SOUND_CAP_EXCL_INPUT) {
    GList *track;

    for (track = oss->tracklist; track != NULL; track = track->next) {
      GstMixerTrack *turn = (GstMixerTrack *) track->data;

      turn->flags &= ~GST_MIXER_TRACK_RECORD;
    }
    oss->recdevs = 0;
  }

  /* set new record bit, if needed */
  if (record) {
    oss->recdevs |= (1 << osstrack->track_num);
  } else {
    oss->recdevs &= ~(1 << osstrack->track_num);
  }

  /* set it to the device */
  if (ioctl (oss->mixer_fd, SOUND_MIXER_WRITE_RECSRC, &oss->recdevs) < 0) {
    g_warning ("Error setting mixer recording devices (0x%x): %s",
        oss->recdevs, strerror (errno));
    return;
  }

  if (record) {
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }
}

void
gst_ossmixer_build_list (GstOssElement * oss)
{
  gint i, devmask, master = -1;
  const GList *pads = gst_element_get_pad_list (GST_ELEMENT (oss));
  GstPadDirection dir = GST_PAD_UNKNOWN;

#ifdef SOUND_MIXER_INFO
  struct mixer_info minfo;
#endif

  g_return_if_fail (oss->mixer_fd == -1);

  oss->mixer_fd = open (oss->mixer_dev, O_RDWR);
  if (oss->mixer_fd == -1) {
    /* this is valid. OSS devices don't need to expose a mixer */
    GST_DEBUG ("Failed to open mixer device %s, mixing disabled: %s",
        oss->mixer_dev, strerror (errno));
    return;
  }

  /* get direction */
  if (pads && g_list_length ((GList *) pads) == 1)
    dir = GST_PAD_DIRECTION (GST_PAD (pads->data));

  /* get masks */
  if (ioctl (oss->mixer_fd, SOUND_MIXER_READ_RECMASK, &oss->recmask) < 0 ||
      ioctl (oss->mixer_fd, SOUND_MIXER_READ_RECSRC, &oss->recdevs) < 0 ||
      ioctl (oss->mixer_fd, SOUND_MIXER_READ_STEREODEVS, &oss->stereomask) < 0
      || ioctl (oss->mixer_fd, SOUND_MIXER_READ_DEVMASK, &devmask) < 0
      || ioctl (oss->mixer_fd, SOUND_MIXER_READ_CAPS, &oss->mixcaps) < 0) {
    GST_DEBUG ("Failed to get device masks - disabling mixer");
    close (oss->mixer_fd);
    oss->mixer_fd = -1;
    return;
  }

  /* get name */
#ifdef SOUND_MIXER_INFO
  if (ioctl (oss->mixer_fd, SOUND_MIXER_INFO, &minfo) == 0) {
    oss->device_name = g_strdup (minfo.name);
  }
#else
  oss->device_name = g_strdup ("Unknown");
#endif

  /* find master volume */
  if (devmask & SOUND_MASK_VOLUME)
    master = SOUND_MIXER_VOLUME;
  else if (devmask & SOUND_MASK_PCM)
    master = SOUND_MIXER_PCM;
  else if (devmask & SOUND_MASK_SPEAKER)
    master = SOUND_MIXER_SPEAKER;       /* doubtful... */
  /* else: no master, so we won't set any */

  /* build track list */
  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
    if (devmask & (1 << i)) {
      GstMixerTrack *track;
      gboolean input = FALSE, stereo = FALSE, record = FALSE;

      /* track exists, make up capabilities */
      if (MASK_BIT_IS_SET (oss->stereomask, i))
        stereo = TRUE;
      if (MASK_BIT_IS_SET (oss->recmask, i))
        input = TRUE;
      if (MASK_BIT_IS_SET (oss->recdevs, i))
        record = TRUE;

      /* do we want this in our list? */
      if ((dir == GST_PAD_SRC && input == FALSE) ||
          (dir == GST_PAD_SINK && i != SOUND_MIXER_PCM))
        continue;

      /* add track to list */
      track = gst_ossmixer_track_new (oss, i, stereo ? 2 : 1,
          (record ? GST_MIXER_TRACK_RECORD : 0) |
          (input ? GST_MIXER_TRACK_INPUT :
              GST_MIXER_TRACK_OUTPUT) |
          ((master != i) ? 0 : GST_MIXER_TRACK_MASTER));
      oss->tracklist = g_list_append (oss->tracklist, track);
    }
  }
}

void
gst_ossmixer_free_list (GstOssElement * oss)
{
  if (oss->mixer_fd == -1)
    return;

  g_list_foreach (oss->tracklist, (GFunc) g_object_unref, NULL);
  g_list_free (oss->tracklist);
  oss->tracklist = NULL;

  if (oss->device_name) {
    g_free (oss->device_name);
    oss->device_name = NULL;
  }

  close (oss->mixer_fd);
  oss->mixer_fd = -1;
}
