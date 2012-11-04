/*
 * GStreamer SunAudio mixer track implementation
 * Copyright (C) 2009 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 *               Garrett D'Amore <garrett.damore@sun.com>
 *
 * gstsunaudiomixeroptions.c: Sun Audio mixer options object
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
#include <sys/audio.h>
#include <sys/mixer.h>

#include <gst/gst-i18n-plugin.h>

#include "gstsunaudiomixeroptions.h"
#include "gstsunaudiomixertrack.h"

GST_DEBUG_CATEGORY_EXTERN (sunaudio_debug);
#define GST_CAT_DEFAULT sunaudio_debug

static void gst_sunaudiomixer_options_init (GstSunAudioMixerOptions * sun_opts);
static void gst_sunaudiomixer_options_class_init (gpointer g_class,
    gpointer class_data);

static GstMixerOptionsClass *parent_class = NULL;

GType
gst_sunaudiomixer_options_get_type (void)
{
  static GType opts_type = 0;

  if (!opts_type) {
    static const GTypeInfo opts_info = {
      sizeof (GstSunAudioMixerOptionsClass),
      NULL,
      NULL,
      gst_sunaudiomixer_options_class_init,
      NULL,
      NULL,
      sizeof (GstSunAudioMixerOptions),
      0,
      (GInstanceInitFunc) gst_sunaudiomixer_options_init,
    };

    opts_type =
        g_type_register_static (GST_TYPE_MIXER_OPTIONS,
        "GstSunAudioMixerOptions", &opts_info, 0);
  }

  return opts_type;
}

static void
gst_sunaudiomixer_options_class_init (gpointer g_class, gpointer class_data)
{
  parent_class = g_type_class_peek_parent (g_class);
}

static void
gst_sunaudiomixer_options_init (GstSunAudioMixerOptions * sun_opts)
{
}

GstMixerOptions *
gst_sunaudiomixer_options_new (GstSunAudioMixerCtrl * mixer, gint track_num)
{
  GstMixerOptions *opts;
  GstSunAudioMixerOptions *sun_opts;
  GstMixerTrack *track;
  const gchar *label;
  gint i;
  struct audio_info audioinfo;

  if ((mixer == NULL) || (mixer->mixer_fd == -1)) {
    g_warning ("mixer not initialized");
    return NULL;
  }

  if (track_num != GST_SUNAUDIO_TRACK_RECSRC) {
    g_warning ("invalid options track");
    return (NULL);
  }

  label = N_("Record Source");

  opts = g_object_new (GST_TYPE_SUNAUDIO_MIXER_OPTIONS,
      "untranslated-label", label, NULL);
  sun_opts = GST_SUNAUDIO_MIXER_OPTIONS (opts);
  track = GST_MIXER_TRACK (opts);

  GST_DEBUG_OBJECT (opts, "New mixer options, track %d: %s",
      track_num, GST_STR_NULL (label));

  /* save off names for the record sources */
  sun_opts->names[0] = g_quark_from_string (_("Microphone"));
  sun_opts->names[1] = g_quark_from_string (_("Line In"));
  sun_opts->names[2] = g_quark_from_string (_("Internal CD"));
  sun_opts->names[3] = g_quark_from_string (_("SPDIF In"));
  sun_opts->names[4] = g_quark_from_string (_("AUX 1 In"));
  sun_opts->names[5] = g_quark_from_string (_("AUX 2 In"));
  sun_opts->names[6] = g_quark_from_string (_("Codec Loopback"));
  sun_opts->names[7] = g_quark_from_string (_("SunVTS Loopback"));

  /* set basic information */
  track->label = g_strdup (_(label));
  track->num_channels = 0;
  track->min_volume = 0;
  track->max_volume = 0;
  track->flags =
      GST_MIXER_TRACK_INPUT | GST_MIXER_TRACK_WHITELIST |
      GST_MIXER_TRACK_NO_RECORD;

  if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
    g_warning ("Error getting audio device settings");
    g_object_unref (G_OBJECT (sun_opts));
    return NULL;
  }

  sun_opts->avail = audioinfo.record.avail_ports;
  sun_opts->track_num = track_num;

  for (i = 0; i < 8; i++) {
    if ((1 << i) & audioinfo.record.avail_ports) {
      const char *s = g_quark_to_string (sun_opts->names[i]);
      opts->values = g_list_append (opts->values, g_strdup (s));
      GST_DEBUG_OBJECT (opts, "option for track %d: %s",
          track_num, GST_STR_NULL (s));
    }
  }

  return opts;
}
