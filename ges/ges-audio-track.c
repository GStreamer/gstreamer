/* GStreamer Editing Services
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
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

/**
 * SECTION: gesaudiotrack
 * @title: GESAudioTrack
 * @short_description: A standard #GESTrack for raw audio
 *
 * A #GESAudioTrack is a default audio #GESTrack, with a
 * #GES_TRACK_TYPE_AUDIO #GESTrack:track-type and "audio/x-raw(ANY)"
 * #GESTrack:caps.
 *
 * By default, an audio track will have its #GESTrack:restriction-caps
 * set to "audio/x-raw" with the following properties:
 *
 * - format: "S32LE"
 * - channels: 2
 * - rate: 44100
 * - layout: "interleaved"
 *
 * These fields are needed for negotiation purposes, but you can change
 * their values if you wish. It is advised that you do so using
 * ges_track_update_restriction_caps() with new values for the fields you
 * wish to change, and any additional fields you may want to add. Unlike
 * using ges_track_set_restriction_caps(), this will ensure that these
 * default fields will at least have some value set.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-smart-adder.h"
#include "ges-audio-track.h"

#define DEFAULT_CAPS "audio/x-raw"

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define DEFAULT_RESTRICTION_CAPS "audio/x-raw, format=S32LE, channels=2, "\
  "rate=44100, layout=interleaved"
#else
#define DEFAULT_RESTRICTION_CAPS "audio/x-raw, format=S32BE, channels=2, "\
  "rate=44100, layout=interleaved"
#endif

struct _GESAudioTrackPrivate
{
  gpointer nothing;
};

G_DEFINE_TYPE_WITH_PRIVATE (GESAudioTrack, ges_audio_track, GES_TYPE_TRACK);

/****************************************************
 *              Private methods and utils           *
 ****************************************************/
static void
_sync_capsfilter_with_track (GESTrack * track, GstElement * capsfilter)
{
  GstCaps *restriction, *caps;
  gint rate;
  GstStructure *structure;

  g_object_get (track, "restriction-caps", &restriction, NULL);
  if (restriction == NULL)
    return;

  if (gst_caps_get_size (restriction) == 0)
    goto done;

  structure = gst_caps_get_structure (restriction, 0);
  if (!gst_structure_get_int (structure, "rate", &rate))
    goto done;

  caps = gst_caps_new_simple ("audio/x-raw", "rate", G_TYPE_INT, rate, NULL);

  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

done:
  gst_caps_unref (restriction);
}

static void
_track_restriction_changed_cb (GESTrack * track, GParamSpec * arg G_GNUC_UNUSED,
    GstElement * capsfilter)
{
  _sync_capsfilter_with_track (track, capsfilter);
}

static void
_weak_notify_cb (GESTrack * track, GstElement * capsfilter)
{
  g_signal_handlers_disconnect_by_func (track,
      (GCallback) _track_restriction_changed_cb, capsfilter);
}

static GstElement *
create_element_for_raw_audio_gap (GESTrack * track)
{
  GstElement *bin;
  GstElement *capsfilter;

  bin = gst_parse_bin_from_description
      ("audiotestsrc wave=silence name=src ! audioconvert ! audioresample ! audioconvert ! capsfilter name=gapfilter caps=audio/x-raw",
      TRUE, NULL);

  capsfilter = gst_bin_get_by_name (GST_BIN (bin), "gapfilter");
  g_object_weak_ref (G_OBJECT (capsfilter), (GWeakNotify) _weak_notify_cb,
      track);
  g_signal_connect (track, "notify::restriction-caps",
      (GCallback) _track_restriction_changed_cb, capsfilter);

  _sync_capsfilter_with_track (track, capsfilter);

  gst_object_unref (capsfilter);

  return bin;
}


/****************************************************
 *              GObject vmethods implementations    *
 ****************************************************/

static void
ges_audio_track_init (GESAudioTrack * self)
{
  self->priv = ges_audio_track_get_instance_private (self);
}

static void
ges_audio_track_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (ges_audio_track_parent_class)->finalize (object);
}

static void
ges_audio_track_class_init (GESAudioTrackClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
/*   GESTrackClass *parent_class = GES_TRACK_CLASS (klass);
 */

  object_class->finalize = ges_audio_track_finalize;

  GES_TRACK_CLASS (klass)->get_mixing_element = ges_smart_adder_new;
}

/****************************************************
 *              API implementation                  *
 ****************************************************/
/**
 * ges_audio_track_new:
 *
 * Creates a new audio track, with a #GES_TRACK_TYPE_AUDIO
 * #GESTrack:track-type, "audio/x-raw(ANY)" #GESTrack:caps, and
 * "audio/x-raw" #GESTrack:restriction-caps with the properties:
 *
 * - format: "S32LE"
 * - channels: 2
 * - rate: 44100
 * - layout: "interleaved"
 *
 * You should use ges_track_update_restriction_caps() if you wish to
 * modify these fields, or add additional ones.
 *
 * Returns: (transfer floating): The newly created audio track.
 */
GESAudioTrack *
ges_audio_track_new (void)
{
  GESAudioTrack *ret;
  GstCaps *caps = gst_caps_from_string (DEFAULT_CAPS);
  GstCaps *restriction_caps = gst_caps_from_string (DEFAULT_RESTRICTION_CAPS);

  ret = g_object_new (GES_TYPE_AUDIO_TRACK, "caps", caps,
      "track-type", GES_TRACK_TYPE_AUDIO, NULL);

  ges_track_set_create_element_for_gap_func (GES_TRACK (ret),
      create_element_for_raw_audio_gap);

  ges_track_set_restriction_caps (GES_TRACK (ret), restriction_caps);

  gst_caps_unref (caps);
  gst_caps_unref (restriction_caps);

  return ret;
}
