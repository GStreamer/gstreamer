/* GStreamer
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gsturitranscodebin.c:
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
#  include "config.h"
#endif

#include "gsttranscoding.h"
#if HAVE_GETRUSAGE
#include "gst-cpu-throttling-clock.h"
#endif
#include <gst/pbutils/pbutils.h>

#include <gst/pbutils/missing-plugins.h>

GST_DEBUG_CATEGORY_STATIC (gst_uri_transcodebin_debug);
#define GST_CAT_DEFAULT gst_uri_transcodebin_debug

typedef struct
{
  GstPipeline parent;

  GstElement *src;
  gchar *source_uri;

  GstElement *transcodebin;

  GstElement *audio_filter;
  GstElement *video_filter;

  GstEncodingProfile *profile;
  gboolean avoid_reencoding;
  guint wanted_cpu_usage;

  GstElement *sink;
  gchar *dest_uri;

  GstClock *cpu_clock;

} GstUriTranscodeBin;

typedef struct
{
  GstPipelineClass parent;

} GstUriTranscodeBinClass;

/* *INDENT-OFF* */
#define parent_class gst_uri_transcode_bin_parent_class
#define GST_TYPE_URI_TRANSCODE_BIN (gst_uri_transcode_bin_get_type ())
#define GST_URI_TRANSCODE_BIN(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_URI_TRANSCODE_BIN, GstUriTranscodeBin))
#define GST_URI_TRANSCODE_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_URI_TRANSCODE_BIN_TYPE, GstUriTranscodeBinClass))
#define GST_IS_TRANSCODE_BIN(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_URI_TRANSCODE_BIN_TYPE))
#define GST_IS_TRANSCODE_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_URI_TRANSCODE_BIN_TYPE))
#define GST_URI_TRANSCODE_BIN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_URI_TRANSCODE_BIN_TYPE, GstUriTranscodeBinClass))

#define DEFAULT_AVOID_REENCODING   FALSE

G_DEFINE_TYPE (GstUriTranscodeBin, gst_uri_transcode_bin, GST_TYPE_PIPELINE)
enum
{
 PROP_0,
 PROP_PROFILE,
 PROP_SOURCE_URI,
 PROP_DEST_URI,
 PROP_AVOID_REENCODING,
 PROP_SINK,
 PROP_SRC,
 PROP_CPU_USAGE,
 PROP_VIDEO_FILTER,
 PROP_AUDIO_FILTER,
 LAST_PROP
};

static void
post_missing_plugin_error (GstElement * dec, const gchar * element_name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (dec, element_name);
  gst_element_post_message (dec, msg);

  GST_ELEMENT_ERROR (dec, CORE, MISSING_PLUGIN,
      ("Missing element '%s' - check your GStreamer installation.",
          element_name), (NULL));
}
/* *INDENT-ON* */

static gboolean
make_transcodebin (GstUriTranscodeBin * self)
{
  GST_INFO_OBJECT (self, "making new transcodebin");

  self->transcodebin = gst_element_factory_make ("transcodebin", NULL);
  if (!self->transcodebin)
    goto no_decodebin;

  g_object_set (self->transcodebin, "profile", self->profile,
      "video-filter", self->video_filter,
      "audio-filter", self->audio_filter,
      "avoid-reencoding", self->avoid_reencoding, NULL);

  gst_bin_add (GST_BIN (self), self->transcodebin);
  if (!gst_element_link (self->transcodebin, self->sink))
    return FALSE;

  return TRUE;

  /* ERRORS */
no_decodebin:
  {
    post_missing_plugin_error (GST_ELEMENT_CAST (self), "transcodebin");

    GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN, (NULL),
        ("No transcodebin element, check your installation"));

    return FALSE;
  }
}

static gboolean
make_dest (GstUriTranscodeBin * self)
{
  GError *err = NULL;

  if (!gst_uri_is_valid (self->dest_uri))
    goto invalid_uri;

  self->sink = gst_element_make_from_uri (GST_URI_SINK, self->dest_uri,
      "sink", &err);
  if (!self->sink)
    goto no_sink;

  gst_bin_add (GST_BIN (self), self->sink);
  g_object_set (self->sink, "sync", TRUE, "max-lateness", GST_CLOCK_TIME_NONE,
      NULL);
  return TRUE;

invalid_uri:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Invalid URI \"%s\".", self->dest_uri), (NULL));
    g_clear_error (&err);
    return FALSE;
  }

no_sink:
  {
    /* whoops, could not create the source element, dig a little deeper to
     * figure out what might be wrong. */
    if (err != NULL && err->code == GST_URI_ERROR_UNSUPPORTED_PROTOCOL) {
      gchar *prot;

      prot = gst_uri_get_protocol (self->dest_uri);
      if (prot == NULL)
        goto invalid_uri;

      gst_element_post_message (GST_ELEMENT_CAST (self),
          gst_missing_uri_source_message_new (GST_ELEMENT (self), prot));

      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN,
          ("No URI handler implemented for \"%s\".", prot), (NULL));

      g_free (prot);
    } else {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("%s", (err) ? err->message : "URI was not accepted by any element"),
          ("No element accepted URI '%s'", self->dest_uri));
    }

    g_clear_error (&err);

    return FALSE;
  }
}

static gboolean
make_source (GstUriTranscodeBin * self)
{
  GError *err = NULL;

  if (!gst_uri_is_valid (self->source_uri))
    goto invalid_uri;

  self->src = gst_element_make_from_uri (GST_URI_SRC, self->source_uri,
      "src", &err);
  if (!self->src)
    goto no_sink;

  gst_bin_add (GST_BIN (self), self->src);

  if (!gst_element_link (self->src, self->transcodebin))
    return FALSE;

  return TRUE;

invalid_uri:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Invalid URI \"%s\".", self->source_uri), (NULL));
    g_clear_error (&err);
    return FALSE;
  }

no_sink:
  {
    /* whoops, could not create the source element, dig a little deeper to
     * figure out what might be wrong. */
    if (err != NULL && err->code == GST_URI_ERROR_UNSUPPORTED_PROTOCOL) {
      gchar *prot;

      prot = gst_uri_get_protocol (self->source_uri);
      if (prot == NULL)
        goto invalid_uri;

      gst_element_post_message (GST_ELEMENT_CAST (self),
          gst_missing_uri_source_message_new (GST_ELEMENT (self), prot));

      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN,
          ("No URI handler implemented for \"%s\".", prot), (NULL));

      g_free (prot);
    } else {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("%s", (err) ? err->message : "URI was not accepted by any element"),
          ("No element accepted URI '%s'", self->dest_uri));
    }

    g_clear_error (&err);

    return FALSE;
  }
}

static void
remove_all_children (GstUriTranscodeBin * self)
{
  if (self->sink) {
    gst_element_set_state (self->sink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self), self->sink);
    self->sink = NULL;
  }

  if (self->transcodebin) {
    gst_element_set_state (self->transcodebin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self), self->transcodebin);
    self->transcodebin = NULL;
  }

  if (self->src) {
    gst_element_set_state (self->src, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self), self->src);
    self->src = NULL;
  }
}

static GstStateChangeReturn
gst_uri_transcode_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstUriTranscodeBin *self = GST_URI_TRANSCODE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:

      if (!make_dest (self))
        goto setup_failed;

      if (!make_transcodebin (self))
        goto setup_failed;

      if (!make_source (self))
        goto setup_failed;

      if (gst_element_set_state (self->sink,
              GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        GST_ERROR_OBJECT (self,
            "Could not set %" GST_PTR_FORMAT " state to PAUSED", self->sink);
        goto setup_failed;
      }

      if (gst_element_set_state (self->transcodebin,
              GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        GST_ERROR_OBJECT (self,
            "Could not set %" GST_PTR_FORMAT " state to PAUSED",
            self->transcodebin);
        goto setup_failed;
      }

      if (gst_element_set_state (self->src,
              GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        GST_ERROR_OBJECT (self,
            "Could not set %" GST_PTR_FORMAT " state to PAUSED", self->src);
        goto setup_failed;
      }

      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    goto beach;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      remove_all_children (self);
      break;
    default:
      break;
  }

beach:
  return ret;

setup_failed:
  remove_all_children (self);
  return GST_STATE_CHANGE_FAILURE;
}

static void
gst_uri_transcode_bin_constructed (GObject * object)
{
#if HAVE_GETRUSAGE
  GstUriTranscodeBin *self = GST_URI_TRANSCODE_BIN (object);

  self->cpu_clock =
      GST_CLOCK (gst_cpu_throttling_clock_new (self->wanted_cpu_usage));
  gst_pipeline_use_clock (GST_PIPELINE (self), self->cpu_clock);
#endif

  ((GObjectClass *) parent_class)->constructed (object);
}

static void
gst_uri_transcode_bin_dispose (GObject * object)
{
  GstUriTranscodeBin *self = (GstUriTranscodeBin *) object;

  g_clear_object (&self->video_filter);
  g_clear_object (&self->audio_filter);
  g_clear_object (&self->cpu_clock);

  G_OBJECT_CLASS (gst_uri_transcode_bin_parent_class)->dispose (object);
}

static void
gst_uri_transcode_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstUriTranscodeBin *self = GST_URI_TRANSCODE_BIN (object);

  switch (prop_id) {
    case PROP_PROFILE:
      GST_OBJECT_LOCK (self);
      g_value_set_object (value, self->profile);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_DEST_URI:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->dest_uri);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_SOURCE_URI:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->source_uri);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_AVOID_REENCODING:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, self->avoid_reencoding);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_CPU_USAGE:
      GST_OBJECT_LOCK (self);
      g_value_set_uint (value, self->wanted_cpu_usage);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_VIDEO_FILTER:
      GST_OBJECT_LOCK (self);
      g_value_set_object (value, self->video_filter);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_AUDIO_FILTER:
      GST_OBJECT_LOCK (self);
      g_value_set_object (value, self->audio_filter);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_uri_transcode_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstUriTranscodeBin *self = GST_URI_TRANSCODE_BIN (object);

  switch (prop_id) {
    case PROP_PROFILE:
      GST_OBJECT_LOCK (self);
      self->profile = g_value_dup_object (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_DEST_URI:
      GST_OBJECT_LOCK (self);
      g_free (self->dest_uri);
      self->dest_uri = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_SOURCE_URI:
      GST_OBJECT_LOCK (self);
      g_free (self->source_uri);
      self->source_uri = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_AVOID_REENCODING:
      GST_OBJECT_LOCK (self);
      self->avoid_reencoding = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_CPU_USAGE:
#if HAVE_GETRUSAGE
      GST_OBJECT_LOCK (self);
      self->wanted_cpu_usage = g_value_get_uint (value);
      g_object_set (self->cpu_clock, "cpu-usage", self->wanted_cpu_usage, NULL);
      GST_OBJECT_UNLOCK (self);
#else
      GST_ERROR_OBJECT (self,
          "No CPU usage throttling support for that platform");
#endif
      break;
    case PROP_AUDIO_FILTER:
      GST_OBJECT_LOCK (self);
      self->audio_filter = g_value_dup_object (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_VIDEO_FILTER:
      GST_OBJECT_LOCK (self);
      self->video_filter = g_value_dup_object (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_uri_transcode_bin_class_init (GstUriTranscodeBinClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_klass;

  object_class->get_property = gst_uri_transcode_bin_get_property;
  object_class->set_property = gst_uri_transcode_bin_set_property;
  object_class->constructed = gst_uri_transcode_bin_constructed;
  object_class->dispose = gst_uri_transcode_bin_dispose;

  gstelement_klass = (GstElementClass *) klass;
  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_uri_transcode_bin_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_uri_transcodebin_debug, "uritranscodebin", 0,
      "UriTranscodebin element");

  gst_element_class_set_static_metadata (gstelement_klass,
      "URITranscode Bin", "Generic/Bin/Encoding",
      "Autoplug and transcoder media from uris",
      "Thibault Saunier <tsaunier@igalia.com>");

  /**
   * GstUriTranscodeBin:profile:
   *
   * The #GstEncodingProfile to use. This property must be set before going
   * to %GST_STATE_PAUSED or higher.
   */
  g_object_class_install_property (object_class, PROP_PROFILE,
      g_param_spec_object ("profile", "Profile",
          "The GstEncodingProfile to use", GST_TYPE_ENCODING_PROFILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstUriTranscodeBin:source-uri:
   *
   * The URI of the stream to encode
   */
  g_object_class_install_property (object_class, PROP_SOURCE_URI,
      g_param_spec_string ("source-uri", "Source URI", "URI to decode",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstUriTranscodeBin:dest-uri:
   *
   * The destination URI to which the stream should be encoded.
   */
  g_object_class_install_property (object_class, PROP_DEST_URI,
      g_param_spec_string ("dest-uri", "URI", "URI to put output stream",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstUriTranscodeBin:avoid-reencoding:
   *
   * See #encodebin:avoid-reencoding
   */
  g_object_class_install_property (object_class, PROP_AVOID_REENCODING,
      g_param_spec_boolean ("avoid-reencoding", "Avoid re-encoding",
          "Whether to re-encode portions of compatible video streams that lay on segment boundaries",
          DEFAULT_AVOID_REENCODING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CPU_USAGE,
      g_param_spec_uint ("cpu-usage", "cpu-usage",
          "The percentage of CPU to try to use with the processus running the "
          "pipeline driven by the clock", 0, 100,
          100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstUriTranscodeBin:video-filter:
   *
   * Set the video filter element/bin to use.
   */
  g_object_class_install_property (object_class, PROP_VIDEO_FILTER,
      g_param_spec_object ("video-filter", "Video filter",
          "the video filter(s) to apply, if possible",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstUriTranscodeBin:audio-filter:
   *
   * Set the audio filter element/bin to use.
   */
  g_object_class_install_property (object_class, PROP_AUDIO_FILTER,
      g_param_spec_object ("audio-filter", "Audio filter",
          "the audio filter(s) to apply, if possible",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_uri_transcode_bin_init (GstUriTranscodeBin * self)
{
  self->wanted_cpu_usage = 100;
}
