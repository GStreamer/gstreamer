/* GStreamer Editing Services
 * Copyright (C) 2020 Ubicast S.A
 *     Author: Thibault Saunier <tsaunier@igalia.com>
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

#include "ges-internal.h"
#include "ges-uri-source.h"

GST_DEBUG_CATEGORY_STATIC (uri_source_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT uri_source_debug


GstElement *
ges_uri_source_create_source (GESUriSource * self)
{
  GESTrack *track;
  GstElement *decodebin;
  const GstCaps *caps = NULL;

  track = ges_track_element_get_track (self->element);

  self->decodebin = decodebin = gst_element_factory_make ("uridecodebin", NULL);
  GST_DEBUG_OBJECT (self->element,
      "%" GST_PTR_FORMAT " - Track! %" GST_PTR_FORMAT, self->decodebin, track);

  if (track)
    caps = ges_track_get_caps (track);

  g_object_set (decodebin, "caps", caps,
      "expose-all-streams", FALSE, "uri", self->uri, NULL);

  return decodebin;

}

static void
ges_uri_source_track_set_cb (GESTrackElement * element,
    GParamSpec * arg G_GNUC_UNUSED, GESUriSource * self)
{
  GESTrack *track;
  const GstCaps *caps = NULL;

  if (!self->decodebin)
    return;

  track = ges_track_element_get_track (GES_TRACK_ELEMENT (element));
  if (!track)
    return;

  caps = ges_track_get_caps (track);

  GST_INFO_OBJECT (element,
      "Setting %" GST_PTR_FORMAT "caps to: %" GST_PTR_FORMAT, self->decodebin,
      caps);
  g_object_set (self->decodebin, "caps", caps, NULL);
}



void
ges_uri_source_init (GESTrackElement * element, GESUriSource * self)
{
  static gsize once = 0;

  if (g_once_init_enter (&once)) {
    GST_DEBUG_CATEGORY_INIT (uri_source_debug, "gesurisource", 0,
        "GES uri source");
    g_once_init_leave (&once, 1);
  }

  self->element = element;
  g_signal_connect (element, "notify::track",
      G_CALLBACK (ges_uri_source_track_set_cb), self);
}
