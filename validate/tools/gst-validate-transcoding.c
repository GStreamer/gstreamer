/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-transcoding.c - CLI tool to validate transcoding operations
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/validate/validate.h>
#include <gst/pbutils/encoding-profile.h>


#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include <gst/validate/gst-validate-scenario.h>

static gint ret = 0;
static GMainLoop *mainloop;
static GstElement *pipeline, *encodebin;
static GstEncodingProfile *encoding_profile = NULL;
static gboolean eos_on_shutdown = FALSE;
static gboolean force_reencoding = FALSE;
static GList *all_raw_caps = NULL;

static gboolean buffering = FALSE;
static gboolean is_live = FALSE;

typedef struct
{
  volatile gint refcount;

  GstSegment segment;           /* The currently configured segment */

  /* FIXME Do we need a weak ref here? */
  GstValidateScenario *scenario;
  guint count_bufs;
  gboolean seen_event;
  GstClockTime running_time;

  /* Make sure to remove all probes when we are done */
  gboolean done;

} KeyUnitProbeInfo;

/* This is used to
 *  1) Make sure we receive the event
 *  2) Count the number of frames that were not KF seen after the event
 */
#define FORCE_KF_DATA_NAME "force-key-unit"
#define NOT_KF_AFTER_FORCE_KF_EVT_TOLERANCE 1

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  g_print ("interrupt received.\n");

  if (eos_on_shutdown) {
    g_print ("Sending EOS to the pipeline\n");
    eos_on_shutdown = FALSE;
    gst_element_send_event (GST_ELEMENT_CAST (user_data), gst_event_new_eos ());
    return TRUE;
  }
  g_main_loop_quit (mainloop);

  /* remove signal handler */
  return FALSE;
}
#endif /* G_OS_UNIX */

static void
key_unit_data_unref (KeyUnitProbeInfo * info)
{
  if (G_UNLIKELY (g_atomic_int_dec_and_test (&info->refcount))) {
    g_slice_free (KeyUnitProbeInfo, info);
  }
}

static KeyUnitProbeInfo *
key_unit_data_ref (KeyUnitProbeInfo * info)
{
  g_atomic_int_inc (&info->refcount);

  return info;
}

static KeyUnitProbeInfo *
key_unit_data_new (GstValidateScenario * scenario, GstClockTime running_time)
{
  KeyUnitProbeInfo *info = g_slice_new0 (KeyUnitProbeInfo);
  info->refcount = 1;

  info->scenario = scenario;
  info->running_time = running_time;

  return info;
}

static GstPadProbeReturn
_check_is_key_unit_cb (GstPad * pad, GstPadProbeInfo * info,
    KeyUnitProbeInfo * kuinfo)
{
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    if (gst_video_event_is_force_key_unit (GST_PAD_PROBE_INFO_DATA (info)))
      kuinfo->seen_event = TRUE;
    else if (GST_EVENT_TYPE (info->data) == GST_EVENT_SEGMENT &&
        GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
      const GstSegment *segment = NULL;

      gst_event_parse_segment (info->data, &segment);
      kuinfo->segment = *segment;
    }
  } else if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))
      && kuinfo->seen_event) {

    if (GST_CLOCK_TIME_IS_VALID (kuinfo->running_time)) {
      GstClockTime running_time = gst_segment_to_running_time (&kuinfo->segment,
          GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (info->data));

      if (running_time < kuinfo->running_time)
        return GST_PAD_PROBE_OK;
    }

    if (GST_BUFFER_FLAG_IS_SET (GST_PAD_PROBE_INFO_BUFFER (info),
            GST_BUFFER_FLAG_DELTA_UNIT)) {
      if (kuinfo->count_bufs >= NOT_KF_AFTER_FORCE_KF_EVT_TOLERANCE) {
        GST_VALIDATE_REPORT (kuinfo->scenario,
            SCENARIO_ACTION_EXECUTION_ERROR,
            "Did not receive a key frame after requested one, "
            " at running_time %" GST_TIME_FORMAT " (with a %i "
            "frame tolerance)", GST_TIME_ARGS (kuinfo->running_time),
            NOT_KF_AFTER_FORCE_KF_EVT_TOLERANCE);

        return GST_PAD_PROBE_REMOVE;
      }

      kuinfo->count_bufs++;
    } else {
      GST_DEBUG_OBJECT (kuinfo->scenario,
          "Properly got keyframe after \"force-keyframe\" event "
          "with running_time %" GST_TIME_FORMAT " (latency %d frame(s))",
          GST_TIME_ARGS (kuinfo->running_time), kuinfo->count_bufs);

      return GST_PAD_PROBE_REMOVE;
    }
  }

  return GST_PAD_PROBE_OK;
}

static int
_find_video_encoder (GValue * velement, gpointer udata)
{
  GstElement *element = g_value_get_object (velement);

  const gchar *klass =
      gst_element_class_get_metadata (GST_ELEMENT_GET_CLASS (element),
      GST_ELEMENT_METADATA_KLASS);

  if (g_strstr_len (klass, -1, "Video") && g_strstr_len (klass, -1, "Encoder")) {

    return 0;
  }

  return !0;
}


static gboolean
_execute_request_key_unit (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  guint count;
  GstIterator *iter;
  gboolean all_headers;

  gboolean ret = TRUE;
  GValue result = { 0, };
  GstEvent *event = NULL;
  GstQuery *segment_query;
  KeyUnitProbeInfo *info = NULL;
  GstElement *video_encoder = NULL;
  GstPad *pad = NULL, *encoder_srcpad = NULL;
  GstClockTime running_time = GST_CLOCK_TIME_NONE;
  const gchar *direction = gst_structure_get_string (action->structure,
      "direction");

  iter = gst_bin_iterate_recurse (GST_BIN (encodebin));
  if (!gst_iterator_find_custom (iter,
          (GCompareFunc) _find_video_encoder, &result, NULL)) {
    g_error ("Could not find any video encode");

    goto fail;
  }

  gst_iterator_free (iter);
  video_encoder = g_value_get_object (&result);
  encoder_srcpad = gst_element_get_static_pad (video_encoder, "src");

  if (!encoder_srcpad) {
    GST_FIXME ("Implement weird encoder management");
    g_error ("We do not handle encoder with not static srcpad");

    goto fail;
  }

  gst_validate_action_get_clocktime (scenario, action,
      "running-time", &running_time);

  if (gst_structure_get_boolean (action->structure, "all-headers",
          &all_headers)) {
    g_error ("Missing field: all-headers");

    goto fail;
  }

  if (!gst_structure_get_uint (action->structure, "count", &count)) {
    if (!gst_structure_get_int (action->structure, "count", (gint *) & count)) {
      g_error ("Missing field: count");

      goto fail;
    }
  }

  info = key_unit_data_new (scenario, running_time);
  if (g_strcmp0 (direction, "upstream") == 0) {
    event = gst_video_event_new_upstream_force_key_unit (running_time,
        all_headers, count);

    pad = gst_element_get_static_pad (video_encoder, "src");
    gst_pad_add_probe (encoder_srcpad,
        GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
        (GstPadProbeCallback) _check_is_key_unit_cb,
        key_unit_data_ref (info), (GDestroyNotify) key_unit_data_unref);
  } else if (g_strcmp0 (direction, "downstream") == 0) {
    GstClockTime timestamp = GST_CLOCK_TIME_NONE,
        stream_time = GST_CLOCK_TIME_NONE;

    pad = gst_element_get_static_pad (video_encoder, "sink");
    if (!pad) {
      GST_FIXME ("Implement weird encoder management");
      g_error ("We do not handle encoder with not static sinkpad");

      goto fail;
    }

    gst_validate_action_get_clocktime (scenario, action,
        "timestamp", &timestamp);

    gst_validate_action_get_clocktime (scenario, action,
        "stream-time", &stream_time);

    event =
        gst_video_event_new_downstream_force_key_unit (timestamp, stream_time,
        running_time, all_headers, count);

    gst_pad_add_probe (pad,
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        (GstPadProbeCallback) _check_is_key_unit_cb,
        key_unit_data_ref (info), (GDestroyNotify) key_unit_data_unref);
  } else {
    g_error ("request keyunit direction %s invalide (should be in"
        " [downstrean, upstream]", direction);

    goto fail;
  }

  gst_validate_printf (action, "Sendings a \"force key unit\" event %s\n",
      direction);

  segment_query = gst_query_new_segment (GST_FORMAT_TIME);
  gst_pad_query (encoder_srcpad, segment_query);

  gst_query_parse_segment (segment_query, &(info->segment.rate),
      &(info->segment.format),
      (gint64 *) & (info->segment.start), (gint64 *) & (info->segment.stop));

  gst_pad_add_probe (encoder_srcpad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) _check_is_key_unit_cb, info,
      (GDestroyNotify) key_unit_data_unref);


  if (!gst_pad_send_event (pad, event)) {
    GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not send \"force key unit\" event %s", direction);
    goto fail;
  }

done:
  if (video_encoder)
    gst_object_unref (video_encoder);
  if (pad)
    gst_object_unref (pad);
  if (encoder_srcpad)
    gst_object_unref (encoder_srcpad);

  return ret;

fail:
  ret = FALSE;
  goto done;
}

static gboolean
_execute_set_restriction (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstCaps *caps;
  GType profile_type = G_TYPE_NONE;
  const gchar *restriction_caps, *profile_type_name, *profile_name;

  restriction_caps =
      gst_structure_get_string (action->structure, "restriction-caps");
  profile_type_name =
      gst_structure_get_string (action->structure, "profile-type");
  profile_name = gst_structure_get_string (action->structure, "profile-name");

  if (profile_type_name) {
    profile_type = g_type_from_name (profile_type_name);

    if (profile_type == G_TYPE_NONE) {
      g_error ("Profile name %s not known", profile_name);

      return FALSE;
    } else if (profile_type == GST_TYPE_ENCODING_CONTAINER_PROFILE) {
      g_error ("Can not set restrictions on container profiles");

      return FALSE;
    }
  } else if (profile_name == NULL) {
    if (g_strrstr (restriction_caps, "audio/x-raw") == restriction_caps)
      profile_type = GST_TYPE_ENCODING_AUDIO_PROFILE;
    else if (g_strrstr (restriction_caps, "video/x-raw") == restriction_caps)
      profile_type = GST_TYPE_ENCODING_VIDEO_PROFILE;
    else {
      g_error
          ("No information on what profiles to apply action, you should set either"
          "profile_name or profile_type_name and the caps %s give us no hint",
          restriction_caps);

      return FALSE;
    }
  }

  caps = gst_caps_from_string (restriction_caps);
  if (caps == NULL) {
    g_error ("Could not parse caps: %s", restriction_caps);

    return FALSE;
  }

  if (GST_IS_ENCODING_CONTAINER_PROFILE (encoding_profile)) {
    gboolean found = FALSE;
    const GList *tmp;

    for (tmp =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (encoding_profile)); tmp;
        tmp = tmp->next) {
      GstEncodingProfile *profile = tmp->data;

      if (profile_type != G_TYPE_NONE
          && G_OBJECT_TYPE (profile) == profile_type) {
        gst_encoding_profile_set_restriction (profile, gst_caps_copy (caps));
        found = TRUE;
      } else if (profile_name
          && g_strcmp0 (gst_encoding_profile_get_name (profile),
              profile_name) == 0) {
        gst_encoding_profile_set_restriction (profile, gst_caps_copy (caps));
        found = TRUE;
      }
    }

    if (!found) {
      g_error ("Could not find profile for %s%s",
          profile_type_name ? profile_type_name : "",
          profile_name ? profile_name : "");

      gst_caps_unref (caps);
      return FALSE;

    }
  }

  if (profile_type != G_TYPE_NONE) {
    gst_validate_printf (action,
        "setting caps to %s on profiles of type %s\n",
        restriction_caps, g_type_name (profile_type));
  } else {
    gst_validate_printf (action, "setting caps to %s on profile %s\n",
        restriction_caps, profile_name);

  }

  gst_caps_unref (caps);
  return TRUE;
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED:
    {
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
        gchar *dotname;
        GstState old, new, pending;

        gst_message_parse_state_changed (message, &old, &new, &pending);

        if (new == GST_STATE_PLAYING) {
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate-transcode.playing");
        }

        dotname = g_strdup_printf ("gst-validate-transcoding.%s_%s",
            gst_element_state_get_name (old), gst_element_state_get_name (new));

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, dotname);
        g_free (dotname);
      }
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      if (!g_getenv ("GST_VALIDATE_SCENARIO"))
        g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      if (!buffering) {
        g_print ("\n");
      }

      gst_message_parse_buffering (message, &percent);
      g_print ("%s %d%%  \r", "Buffering...", percent);

      /* no state management needed for live pipelines */
      if (is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (buffering) {
          buffering = FALSE;
          gst_element_set_state (pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering... */
        if (!buffering) {
          gst_element_set_state (pipeline, GST_STATE_PAUSED);
          buffering = TRUE;
        }
      }
      break;
    }
    case GST_MESSAGE_REQUEST_STATE:
    {
      GstState state;

      gst_message_parse_request_state (message, &state);

      if (GST_IS_VALIDATE_SCENARIO (GST_MESSAGE_SRC (message))
          && state == GST_STATE_NULL) {
        GST_VALIDATE_REPORT (GST_MESSAGE_SRC (message),
            SCENARIO_ACTION_EXECUTION_ISSUE,
            "Force stopping a transcoding pipeline is not recommanded"
            " you should make sure to finalize it using a EOS event");

        gst_validate_printf (pipeline, "State change request NULL, "
            "quiting mainloop\n");
        g_main_loop_quit (mainloop);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
pad_added_cb (GstElement * uridecodebin, GstPad * pad, GstElement * encodebin)
{
  GstCaps *caps;
  GstPad *sinkpad = NULL;

  caps = gst_pad_query_caps (pad, NULL);

  /* Ask encodebin for a compatible pad */
  GST_DEBUG_OBJECT (uridecodebin, "Pad added, caps: %" GST_PTR_FORMAT, caps);

  g_signal_emit_by_name (encodebin, "request-pad", caps, &sinkpad);
  if (caps)
    gst_caps_unref (caps);

  if (sinkpad == NULL) {
    GST_WARNING ("Couldn't get an encoding pad for pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    return;
  }

  if (G_UNLIKELY (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)) {
    GstCaps *othercaps = gst_pad_get_current_caps (sinkpad);
    caps = gst_pad_get_current_caps (pad);

    GST_ERROR ("Couldn't link pads \n\n%" GST_PTR_FORMAT "\n\n  and \n\n %"
        GST_PTR_FORMAT "\n\n", caps, othercaps);

    gst_caps_unref (caps);
    gst_caps_unref (othercaps);
  }

  gst_object_unref (sinkpad);
  return;
}

static void
create_transcoding_pipeline (gchar * uri, gchar * outuri)
{
  GstElement *src, *sink;

  pipeline = gst_pipeline_new ("encoding-pipeline");
  src = gst_element_factory_make ("uridecodebin", NULL);

  encodebin = gst_element_factory_make ("encodebin", NULL);
  g_object_set (encodebin, "avoid-reencoding", !force_reencoding, NULL);
  sink = gst_element_make_from_uri (GST_URI_SINK, outuri, "sink", NULL);
  g_assert (sink);

  g_object_set (src, "uri", uri, NULL);
  g_object_set (encodebin, "profile", encoding_profile, NULL);

  g_signal_connect (src, "pad-added", G_CALLBACK (pad_added_cb), encodebin);

  gst_bin_add_many (GST_BIN (pipeline), src, encodebin, sink, NULL);
  gst_element_link (encodebin, sink);
}

static gboolean
_parse_encoding_profile (const gchar * option_name, const gchar * value,
    gpointer udata, GError ** error)
{
  GstCaps *caps;
  char *preset_name = NULL;
  gchar **restriction_format, **preset_v;

  guint i, presence = 0;
  GstCaps *restrictioncaps = NULL;
  gchar **strpresence_v, **strcaps_v = g_strsplit (value, ":", 0);

  if (strcaps_v[0] && *strcaps_v[0]) {
    caps = gst_caps_from_string (strcaps_v[0]);
    if (caps == NULL) {
      g_printerr ("Could not parse caps %s", strcaps_v[0]);
      return FALSE;
    }
    encoding_profile =
        GST_ENCODING_PROFILE (gst_encoding_container_profile_new
        ("User profile", "User profile", caps, NULL));
    gst_caps_unref (caps);
  } else {
    encoding_profile = NULL;
  }

  for (i = 1; strcaps_v[i]; i++) {
    GstEncodingProfile *profile = NULL;
    gchar *strcaps, *strpresence;

    restriction_format = g_strsplit (strcaps_v[i], "->", 0);
    if (restriction_format[1]) {
      restrictioncaps = gst_caps_from_string (restriction_format[0]);
      strcaps = g_strdup (restriction_format[1]);
    } else {
      restrictioncaps = NULL;
      strcaps = g_strdup (restriction_format[0]);
    }
    g_strfreev (restriction_format);

    preset_v = g_strsplit (strcaps, "+", 0);
    if (preset_v[1]) {
      strpresence = preset_v[1];
      g_free (strcaps);
      strcaps = g_strdup (preset_v[0]);
    } else {
      strpresence = preset_v[0];
    }

    strpresence_v = g_strsplit (strpresence, "|", 0);
    if (strpresence_v[1]) {     /* We have a presence */
      gchar *endptr;

      if (preset_v[1]) {        /* We have preset and presence */
        preset_name = g_strdup (strpresence_v[0]);
      } else {                  /* We have a presence but no preset */
        g_free (strcaps);
        strcaps = g_strdup (strpresence_v[0]);
      }

      presence = strtoll (strpresence_v[1], &endptr, 10);
      if (endptr == strpresence_v[1]) {
        g_printerr ("Wrong presence %s\n", strpresence_v[1]);

        return FALSE;
      }
    } else {                    /* We have no presence */
      if (preset_v[1]) {        /* Not presence but preset */
        preset_name = g_strdup (preset_v[1]);
        g_free (strcaps);
        strcaps = g_strdup (preset_v[0]);
      }                         /* Else we have no presence nor preset */
    }
    g_strfreev (strpresence_v);
    g_strfreev (preset_v);

    GST_DEBUG ("Creating preset with restrictions: %" GST_PTR_FORMAT
        ", caps: %s, preset %s, presence %d", restrictioncaps, strcaps,
        preset_name ? preset_name : "none", presence);

    caps = gst_caps_from_string (strcaps);
    g_free (strcaps);
    if (caps == NULL) {
      g_warning ("Could not create caps for %s", strcaps_v[i]);

      return FALSE;
    }

    all_raw_caps = g_list_append (all_raw_caps, gst_caps_copy (caps));
    if (g_str_has_prefix (strcaps_v[i], "audio/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_audio_profile_new (caps,
              preset_name, restrictioncaps, presence));
    } else if (g_str_has_prefix (strcaps_v[i], "video/") ||
        g_str_has_prefix (strcaps_v[i], "image/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_video_profile_new (caps,
              preset_name, restrictioncaps, presence));
    }

    g_free (preset_name);
    gst_caps_unref (caps);
    if (restrictioncaps)
      gst_caps_unref (restrictioncaps);

    if (profile == NULL) {
      g_warning ("No way to create a preset for caps: %s", strcaps_v[i]);

      return FALSE;
    }

    if (encoding_profile) {
      if (gst_encoding_container_profile_add_profile
          (GST_ENCODING_CONTAINER_PROFILE (encoding_profile),
              profile) == FALSE) {
        g_warning ("Can not create a preset for caps: %s", strcaps_v[i]);

        return FALSE;
      }
    } else {
      encoding_profile = profile;
    }
  }
  g_strfreev (strcaps_v);

  return TRUE;
}

static void
_register_actions (void)
{
/* *INDENT-OFF* */
  gst_validate_register_action_type ("set-restriction", "validate-transcoding", _execute_set_restriction,
      (GstValidateActionParameter []) {
        {
          .name = "restriction-caps",
          .description = "The restriction caps to set on the encodebin"
                         "encoding profile.\nSee gst_encoding_profile_set_restriction()",
          .mandatory = TRUE,
          .types = "GstCaps serialized as a string"
        },
        {NULL}
      },
      "Change the restriction caps on the fly",
      FALSE);

  gst_validate_register_action_type ("video-request-key-unit", "validate-transcoding",
      _execute_request_key_unit,
      (GstValidateActionParameter []) {
        {
          .name = "direction",
          .description = "The direction for the event to travel, should be in\n"
                          "  * [upstream, downstream]",
          .mandatory = TRUE,
          .types = "string",
          NULL
        },
        {
          .name = "running-time",
          .description = "The running_time can be set to request a new key unit at a specific running_time.\n"
                          "If not set, GST_CLOCK_TIME_NONE will be used so upstream elements will produce a new key unit"
                          "as soon as possible.",
          .mandatory = FALSE,
          .types = "double or string",
          .possible_variables = "position: The current position in the stream\n"
            "duration: The duration of the stream",
          NULL
        },
        {
          .name = "all-headers",
          .description = "TRUE to produce headers when starting a new key unit",
          .mandatory = TRUE,
          .types = "boolean",
          NULL
        },
        {
          .name = "count",
          .description = "integer that can be used to number key units",
          .mandatory = TRUE,
          .types = "int",
          NULL
        },
        {NULL}
      },
      "Request a video key unit", FALSE);
/* *INDENT-ON* */
}

int
main (int argc, gchar ** argv)
{
  guint i;
  GstBus *bus;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GOptionContext *ctx;
  int rep_err;
  GstStateChangeReturn sret;
  gchar *output_file = NULL;

#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif

  GError *err = NULL;
  const gchar *scenario = NULL, *configs = NULL;
  gboolean want_help = FALSE;
  gboolean list_scenarios = FALSE, inspect_action_type = FALSE;

  GOptionEntry options[] = {
    {"output-format", 'o', 0, G_OPTION_ARG_CALLBACK, &_parse_encoding_profile,
          "Set the properties to use for the encoding profile "
          "(in case of transcoding.) For example:\n"
          "video/mpegts:video/x-raw-yuv,width=1920,height=1080->video/x-h264:audio/x-ac3\n"
          "A preset name can be used by adding +presetname, eg:\n"
          "video/webm:video/x-vp8+mypreset:audio/x-vorbis\n"
          "The presence property of the profile can be specified with |<presence>, eg:\n"
          "video/webm:video/x-vp8|<presence>:audio/x-vorbis\n",
        "properties-values"},
    {"set-scenario", '\0', 0, G_OPTION_ARG_STRING, &scenario,
        "Let you set a scenario, it can be a full path to a scenario file"
          " or the name of the scenario (name of the file without the"
          " '.scenario' extension).", NULL},
    {"set-configs", '\0', 0, G_OPTION_ARG_STRING, &configs,
          "Let you set a config scenario, the scenario needs to be set as 'config"
          "' you can specify a list of scenario separated by ':'"
          " it will override the GST_VALIDATE_SCENARIO environment variable,",
        NULL},
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        "If an EOS event should be sent to the pipeline if an interrupt is "
          "received, instead of forcing the pipeline to stop. Sending an EOS "
          "will allow the transcoding to finish the files properly before "
          "exiting.", NULL},
    {"list-scenarios", 'l', 0, G_OPTION_ARG_NONE, &list_scenarios,
        "List the avalaible scenarios that can be run", NULL},
    {"inspect-action-type", 't', 0, G_OPTION_ARG_NONE, &inspect_action_type,
          "Inspect the avalaible action types with which to write scenarios"
          " if no parameter passed, it will list all avalaible action types"
          " otherwize will print the full description of the wanted types",
        NULL},
    {"scenarios-defs-output-file", '\0', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store scenarios details. "
          "Implies --list-scenario",
        NULL},
    {"force-reencoding", 'r', 0, G_OPTION_ARG_NONE, &force_reencoding,
        "Whether to try to force reencoding, meaning trying to only remux "
          "if possible(default: TRUE)", NULL},
    {NULL}
  };

  /* There is a bug that make gst_init remove the help param when initializing,
   * it is FIXED in 1.0 */
  for (i = 1; i < argc; i++) {
    if (!g_strcmp0 (argv[i], "--help") || !g_strcmp0 (argv[i], "-h"))
      want_help = TRUE;
  }

  if (!want_help)
    gst_init (&argc, &argv);

  g_set_prgname ("gst-validate-transcoding-" GST_API_VERSION);
  ctx = g_option_context_new ("[input-uri] [output-uri]");
  g_option_context_set_summary (ctx, "Transcodes input-uri to output-uri, "
      "using the given encoding profile. The pipeline will be monitored for "
      "possible issues detection using the gst-validate lib."
      "\nCan also perform file conformance"
      "tests after transcoding to make sure the result is correct");
  g_option_context_add_main_entries (ctx, options, NULL);
  if (want_help) {
    g_option_context_add_group (ctx, gst_init_get_option_group ());
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  if (scenario || configs) {
    gchar *scenarios;

    if (scenario)
      scenarios = g_strjoin (":", scenario, configs, NULL);
    else
      scenarios = g_strdup (configs);

    g_setenv ("GST_VALIDATE_SCENARIO", scenarios, TRUE);
    g_free (scenarios);
  }

  gst_validate_init ();

  if (list_scenarios || output_file) {
    if (gst_validate_list_scenarios (argv + 1, argc - 1, output_file))
      return 1;
    return 0;
  }


  _register_actions ();

  if (inspect_action_type) {
    if (gst_validate_print_action_types ((const gchar **) argv + 1, argc - 1))
      return 0;

    return -1;
  }

  if (argc != 3) {
    g_printerr ("%i arguments recived, 2 expected.\n"
        "You should run the test using:\n"
        "    ./gst-validate-transcoding-0.10 <input-uri> <output-uri> [options]\n",
        argc - 1);
    return 1;
  }

  if (encoding_profile == NULL) {
    GST_INFO ("Creating default encoding profile");

    _parse_encoding_profile ("encoding-profile",
        "application/ogg:video/x-theora:audio/x-vorbis", NULL, NULL);
  }

  /* Create the pipeline */
  create_transcoding_pipeline (argv[1], argv[2]);

#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  runner = gst_validate_runner_new ();
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));
  mainloop = g_main_loop_new (NULL, FALSE);

  if (!runner) {
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) bus_callback, mainloop);

  g_print ("Starting pipeline\n");
  sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, we should get an error message posted on the bus */
      g_print ("Pipeline failed to go to PLAYING state\n");
      ret = -1;
      goto exit;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live.\n");
      is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Prerolling...\r");
      break;
    default:
      break;
  }

  g_main_loop_run (mainloop);

  rep_err = gst_validate_runner_exit (runner, TRUE);
  if (ret == 0)
    ret = rep_err;

exit:
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (pipeline);
  g_object_unref (monitor);
  g_object_unref (runner);

#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif
  gst_deinit ();
  gst_validate_deinit ();

  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);
  return ret;
}
