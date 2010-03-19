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
# include <sys/soundcard.h>
#else
# ifdef HAVE_OSS_INCLUDE_IN_ROOT
#  include <soundcard.h>
# else
#  ifdef HAVE_OSS_INCLUDE_IN_MACHINE
#   include <machine/soundcard.h>
#  else
#   error "What to include?"
#  endif /* HAVE_OSS_INCLUDE_IN_MACHINE */
# endif /* HAVE_OSS_INCLUDE_IN_ROOT */
#endif /* HAVE_OSS_INCLUDE_IN_SYS */

#include <gst/gst-i18n-plugin.h>

#include "gstossmixertrack.h"

GST_DEBUG_CATEGORY_EXTERN (oss_debug);
#define GST_CAT_DEFAULT oss_debug

#define MASK_BIT_IS_SET(mask, bit) \
  (mask & (1 << bit))

G_DEFINE_TYPE (GstOssMixerTrack, gst_ossmixer_track, GST_TYPE_MIXER_TRACK);

static void
gst_ossmixer_track_class_init (GstOssMixerTrackClass * klass)
{
  /* nop */
}

static void
gst_ossmixer_track_init (GstOssMixerTrack * track)
{
  track->lvol = track->rvol = 0;
  track->track_num = 0;
}

static const gchar **labels = NULL;

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
  const gchar *origs[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_LABELS;
  const struct
  {
    const gchar *given;
    const gchar *wanted;
  }
  cases[] = {
    /* Note: this list is simply ripped from soundcard.h. For
     * some people, some values might be missing (3D surround,
     * etc.) - feel free to add them. That's the reason why
     * I'm doing this in such a horribly complicated way. */
    {
    "Vol  ", _("Volume")}, {
    "Bass ", _("Bass")}, {
    "Trebl", _("Treble")}, {
    "Synth", _("Synth")}, {
    "Pcm  ", _("PCM")}, {
    "Spkr ", _("Speaker")}, {
    "Line ", _("Line-in")}, {
    "Mic  ", _("Microphone")}, {
    "CD   ", _("CD")}, {
    "Mix  ", _("Mixer")}, {
    "Pcm2 ", _("PCM-2")}, {
    "Rec  ", _("Record")}, {
    "IGain", _("In-gain")}, {
    "OGain", _("Out-gain")}, {
    "Line1", _("Line-1")}, {
    "Line2", _("Line-2")}, {
    "Line3", _("Line-3")}, {
    "Digital1", _("Digital-1")}, {
    "Digital2", _("Digital-2")}, {
    "Digital3", _("Digital-3")}, {
    "PhoneIn", _("Phone-in")}, {
    "PhoneOut", _("Phone-out")}, {
    "Video", _("Video")}, {
    "Radio", _("Radio")}, {
    "Monitor", _("Monitor")}, {
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

GstMixerTrack *
gst_ossmixer_track_new (gint mixer_fd,
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
  if (ioctl (mixer_fd, MIXER_READ (osstrack->track_num), &volume) < 0) {
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
