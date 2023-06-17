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

#define DEFAULT_RAW_CAPS			\
  "video/x-raw; "				\
  "audio/x-raw; "				\
  "text/x-raw; "				\
  "subpicture/x-dvd; "			\
  "subpicture/x-pgs"

static GstStaticCaps default_raw_caps = GST_STATIC_CAPS (DEFAULT_RAW_CAPS);

static inline gboolean
are_raw_caps (const GstCaps * caps)
{
  GstCaps *raw = gst_static_caps_get (&default_raw_caps);
  gboolean res = gst_caps_can_intersect (caps, raw);

  gst_caps_unref (raw);
  return res;
}

typedef enum
{
  GST_AUTOPLUG_SELECT_TRY,
  GST_AUTOPLUG_SELECT_EXPOSE,
  GST_AUTOPLUG_SELECT_SKIP,
} GstAutoplugSelectResult;

static gint
autoplug_select_cb (GstElement * bin, GstPad * pad, GstCaps * caps,
    GstElementFactory * factory, GESUriSource * self)
{
  GstElement *nlesrc;
  GstCaps *downstream_caps;
  GstQuery *segment_query = NULL;
  GstFormat segment_format;
  GstAutoplugSelectResult res = GST_AUTOPLUG_SELECT_TRY;
  gchar *stream_id = gst_pad_get_stream_id (pad);
  const gchar *wanted_id =
      gst_discoverer_stream_info_get_stream_id
      (ges_uri_source_asset_get_stream_info (GES_URI_SOURCE_ASSET
          (ges_extractable_get_asset (GES_EXTRACTABLE (self->element)))));
  gboolean wanted = !g_strcmp0 (stream_id, wanted_id);

  if (!ges_source_get_rendering_smartly (GES_SOURCE (self->element))) {
    if (!are_raw_caps (caps))
      goto done;

    if (!wanted) {
      GST_INFO_OBJECT (self->element, "Not matching stream id: %s -> SKIPPING",
          stream_id);
      res = GST_AUTOPLUG_SELECT_SKIP;
    } else {
      GST_INFO_OBJECT (self->element, "Using stream %s", stream_id);
    }
    goto done;
  }

  segment_query = gst_query_new_segment (GST_FORMAT_TIME);
  if (!gst_pad_query (pad, segment_query)) {
    GST_DEBUG_OBJECT (pad, "Could not query segment");

    goto done;
  }

  gst_query_parse_segment (segment_query, NULL, &segment_format, NULL, NULL);
  if (segment_format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (pad,
        "Segment not in %s != time for %" GST_PTR_FORMAT
        "... continue plugin elements", gst_format_get_name (segment_format),
        caps);

    goto done;
  }

  nlesrc = ges_track_element_get_nleobject (self->element);
  downstream_caps = gst_pad_peer_query_caps (nlesrc->srcpads->data, NULL);
  if (downstream_caps && gst_caps_can_intersect (downstream_caps, caps)) {
    if (wanted) {
      res = GST_AUTOPLUG_SELECT_EXPOSE;
      GST_INFO_OBJECT (self->element,
          "Exposing %" GST_PTR_FORMAT " with stream id: %s", caps, stream_id);
    } else {
      res = GST_AUTOPLUG_SELECT_SKIP;
      GST_DEBUG_OBJECT (self->element, "Totally skipping %s", stream_id);
    }
  }
  gst_clear_caps (&downstream_caps);

done:
  g_free (stream_id);
  gst_clear_query (&segment_query);

  return res;
}

static void
source_setup_cb (GstElement * decodebin, GstElement * source,
    GESUriSource * self)
{
  GstElementFactory *factory = gst_element_get_factory (source);

  if (!factory || g_strcmp0 (GST_OBJECT_NAME (factory), "gessrc")) {
    return;
  }

  GESTrack *track = ges_track_element_get_track (self->element);
  GESTimeline *subtimeline;

  g_object_get (source, "timeline", &subtimeline, NULL);
  GstStreamCollection *subtimeline_collection =
      ges_timeline_get_stream_collection (subtimeline);

  ges_track_select_subtimeline_streams (track, subtimeline_collection,
      GST_ELEMENT (subtimeline));
}

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

  g_signal_connect (decodebin, "source-setup",
      G_CALLBACK (source_setup_cb), self);

  g_object_set (decodebin, "caps", caps,
      "expose-all-streams", FALSE, "uri", self->uri, NULL);
  g_signal_connect (decodebin, "autoplug-select",
      G_CALLBACK (autoplug_select_cb), self);

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

gboolean
ges_uri_source_select_pad (GESSource * self, GstPad * pad)
{
  gboolean res = TRUE;
  gboolean is_nested_timeline;
  GESUriSourceAsset *asset =
      GES_URI_SOURCE_ASSET (ges_extractable_get_asset (GES_EXTRACTABLE (self)));
  const GESUriClipAsset *clip_asset =
      ges_uri_source_asset_get_filesource_asset (asset);
  const gchar *wanted_stream_id = ges_asset_get_id (GES_ASSET (asset));
  gchar *stream_id;

  if (clip_asset) {
    g_object_get (G_OBJECT (clip_asset), "is-nested-timeline",
        &is_nested_timeline, NULL);

    if (is_nested_timeline) {
      GST_DEBUG_OBJECT (self, "Nested timeline track selection is handled"
          " by the timeline SELECT_STREAM events handling.");

      return TRUE;
    }
  }

  stream_id = gst_pad_get_stream_id (pad);
  res = !g_strcmp0 (stream_id, wanted_stream_id);

  GST_INFO_OBJECT (self, "%s pad with stream id: %s as %s wanted",
      res ? "Using" : "Ignoring", stream_id, wanted_stream_id);
  g_free (stream_id);

  return res;
}
