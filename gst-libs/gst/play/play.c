/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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

#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>

#include "play.h"

#define TICK_INTERVAL_MSEC 200

GST_DEBUG_CATEGORY_STATIC (play_debug);
#define GST_CAT_DEFAULT play_debug

enum
{
  TIME_TICK,
  STREAM_LENGTH,
  HAVE_VIDEO_SIZE,
  LAST_SIGNAL
};

struct _GstPlayPrivate
{
  /* progress indicators */
  guint64 time_nanos;
  guint64 length_nanos;
  gint get_length_attempt;
  guint tick_id;

  /* playbin object */
  GstElement *playbin;

  /* visualization */
  gboolean enable_vis;
  GstElement *vis_element;

  /* location */
  char *location;
};

static guint gst_play_signals[LAST_SIGNAL] = { 0 };

static GstPipelineClass *parent_class = NULL;

/* ======================================================= */
/*                                                         */
/*                    Private Methods                      */
/*                                                         */
/* ======================================================= */

static void
caps_set (GstPad * pad, GParamSpec * pspec, GstPlay * play)
{
  const GstCaps *caps = GST_PAD_CAPS (pad);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  if (s) {
    const GValue *par;
    gint w, h;

    /* get effective size */
    gst_structure_get_int (s, "width", &w);
    gst_structure_get_int (s, "height", &h);
    if ((par = gst_structure_get_value (s, "pixel-aspect-ratio"))) {
      gint num = gst_value_get_fraction_numerator (par),
          den = gst_value_get_fraction_denominator (par);

      if (num > den)
        w *= (gfloat) num / den;
      else
        h *= (gfloat) den / num;
    }

    g_signal_emit (play, gst_play_signals[HAVE_VIDEO_SIZE], 0, w, h);
    g_signal_handlers_disconnect_by_func (pad, caps_set, play);
  }
}

static void
setup_size (GstPlay * play)
{
  const GList *streaminfo = NULL;
  GstPad *pad = NULL;

  g_object_get (G_OBJECT (play->priv->playbin),
      "stream-info", &streaminfo, NULL);

  for (; streaminfo != NULL; streaminfo = streaminfo->next) {
    GObject *info = streaminfo->data;
    gint type;
    GParamSpec *pspec;
    GEnumValue *val;

    g_object_get (info, "type", &type, NULL);
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (info), "type");
    val = g_enum_get_value (G_PARAM_SPEC_ENUM (pspec)->enum_class, type);

    if (g_strrstr (val->value_name, "VIDEO")) {
      g_object_get (info, "object", &pad, NULL);
      pad = (GstPad *) GST_PAD_REALIZE (pad);
      break;
    }
  }

  if (pad) {
    /* handle explicit caps as well - they're set later */
    if (GST_PAD_CAPS (pad))
      caps_set (pad, NULL, play);
    else
      g_signal_connect (pad, "notify::caps", G_CALLBACK (caps_set), play);
  }
}

static gboolean
gst_play_tick_callback (GstPlay * play)
{
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 value;

  /* check length/pos of stream */
  if (!GST_CLOCK_TIME_IS_VALID (play->priv->length_nanos) &&
      play->priv->get_length_attempt < 16) {
    if (gst_element_query (GST_ELEMENT (play->priv->playbin),
            GST_QUERY_TOTAL, &fmt, &value)) {
      play->priv->length_nanos = value;
      g_signal_emit (G_OBJECT (play), gst_play_signals[STREAM_LENGTH],
          0, play->priv->length_nanos);
    }
    play->priv->get_length_attempt++;
  }
  if (gst_element_query (GST_ELEMENT (play->priv->playbin),
          GST_QUERY_POSITION, &fmt, &value)) {
    play->priv->time_nanos = value;
    g_signal_emit (G_OBJECT (play), gst_play_signals[TIME_TICK],
        0, play->priv->time_nanos);
  }

  return TRUE;
}

/*
 * Virtual function.
 */

static void
gst_play_change_state (GstElement * element,
    GstElementState old, GstElementState new, gpointer data)
{
  GstPlay *play = GST_PLAY (data);
  gint t = (old << 8) | new;

  /* now set up tickers */
  switch (t) {
    case GST_STATE_PAUSED_TO_PLAYING:
      play->priv->tick_id = g_timeout_add (TICK_INTERVAL_MSEC,
          (GSourceFunc) gst_play_tick_callback, play);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      if (play->priv->tick_id != 0) {
        g_source_remove (play->priv->tick_id);
        play->priv->tick_id = 0;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      play->priv->length_nanos = GST_CLOCK_TIME_NONE;
      play->priv->time_nanos = GST_CLOCK_TIME_NONE;
      play->priv->get_length_attempt = 0;
      break;
    case GST_STATE_READY_TO_PAUSED:
      setup_size (play);
      break;
    default:
      break;
  }
}

/* =========================================== */
/*                                             */
/*         Init & Dispose & Class init         */
/*                                             */
/* =========================================== */

static void
gst_play_dispose (GObject * object)
{
  GstPlay *play = GST_PLAY (object);

  /* reset */
  gst_element_set_state (GST_ELEMENT (play), GST_STATE_NULL);

  /* no vis */
  if (play->priv->vis_element) {
    gst_object_unref (GST_OBJECT (play->priv->vis_element));
    play->priv->vis_element = NULL;
  }

  g_free (play->priv->location);
  play->priv->location = NULL;

  /* parent will clean up the rest */
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_init (GstPlay * play)
{
  play->priv = g_new0 (GstPlayPrivate, 1);

  /* default */
  play->priv->length_nanos = GST_CLOCK_TIME_NONE;
  play->priv->time_nanos = GST_CLOCK_TIME_NONE;
  play->priv->tick_id = 0;
  play->priv->get_length_attempt = 0;

  play->priv->enable_vis = FALSE;
  play->priv->vis_element = NULL;

  play->priv->location = NULL;

  /* make playbin */
  play->priv->playbin = gst_element_factory_make ("playbin", "player");
  if (play->priv->playbin) {
    gst_bin_add (GST_BIN (play), play->priv->playbin);
    g_signal_connect (play->priv->playbin, "state-change",
        G_CALLBACK (gst_play_change_state), play);
  }
}

static void
gst_play_class_init (GstPlayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_PIPELINE);

  gobject_class->dispose = gst_play_dispose;

  gst_play_signals[TIME_TICK] =
      g_signal_new ("time-tick", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstPlayClass, time_tick), NULL, NULL,
      gst_marshal_VOID__INT64, G_TYPE_NONE, 1, G_TYPE_INT64);
  gst_play_signals[STREAM_LENGTH] =
      g_signal_new ("stream-length", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstPlayClass, stream_length), NULL, NULL,
      gst_marshal_VOID__INT64, G_TYPE_NONE, 1, G_TYPE_INT64);
  gst_play_signals[HAVE_VIDEO_SIZE] =
      g_signal_new ("have-video-size", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstPlayClass, have_video_size), NULL, NULL,
      gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  GST_DEBUG_CATEGORY_INIT (play_debug, "GST_PLAY", 0, "GStreamer Play library");

  GST_DEBUG ("Play class initialized");
}

/* ======================================================= */
/*                                                         */
/*                     Public Methods                      */
/*                                                         */
/* ======================================================= */

/**
 * gst_play_set_location:
 * @play: a #GstPlay.
 * @location: a const #char* indicating location to play
 *
 * Set location of @play to @location.
 *
 * Returns: TRUE if location was set successfully.
 */

gboolean
gst_play_set_location (GstPlay * play, const char *location)
{
  char *uri;

  g_return_val_if_fail (play != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLAY (play), FALSE);
  g_return_val_if_fail (location != NULL, FALSE);

  /* cache */
  if (play->priv->location)
    g_free (play->priv->location);
  play->priv->location = g_strdup (location);

  /* get file */
  gst_element_set_state (play->priv->playbin, GST_STATE_READY);
  if (strchr (location, ':'))
    uri = g_strdup (location);
  else if (location[0] == '/')
    uri = g_strdup_printf ("file://%s", location);
  else {
    char cwd[256];

    getcwd (cwd, 255);
    uri = g_strdup_printf ("file://%s/%s", cwd, location);
  }
  g_object_set (play->priv->playbin, "uri", uri, NULL);
  g_free (uri);

  /* open */
  if (gst_element_set_state (play->priv->playbin,
          GST_STATE_PAUSED) != GST_STATE_SUCCESS) {
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_play_get_location:
 * @play: a #GstPlay.
 *
 * Get current location of @play.
 *
 * Returns: a #char* pointer to current location.
 */

char *
gst_play_get_location (GstPlay * play)
{
  g_return_val_if_fail (play != NULL, NULL);
  g_return_val_if_fail (GST_IS_PLAY (play), NULL);

  return g_strdup (play->priv->location);
}

/**
 * gst_play_seek_to_time:
 * @play: a #GstPlay.
 * @time_nanos: a #gint64 indicating a time position.
 *
 * Performs a seek on @play until @time_nanos.
 */

/*
 * FIXME: use GstClockTime for 0.9
 */

gboolean
gst_play_seek_to_time (GstPlay * play, gint64 time_nanos)
{
  return gst_element_seek (play->priv->playbin,
      GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, time_nanos);
}

/**
 * gst_play_set_data_src:
 * @play: a #GstPlay.
 * @data_src: a #GstElement.
 *
 * Set @data_src as the source element of @play.
 *
 * Returns: TRUE if call succeeded.
 */

gboolean
gst_play_set_data_src (GstPlay * play, GstElement * data_src)
{
  /* deprecated - use URIs */
  return FALSE;
}

/**
 * gst_play_set_video_sink:
 * @play: a #GstPlay.
 * @video_sink: a #GstElement.
 *
 * Set @video_sink as the video sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */

gboolean
gst_play_set_video_sink (GstPlay * play, GstElement * video_sink)
{
  g_object_set (G_OBJECT (play->priv->playbin), "video-sink", video_sink, NULL);

  return TRUE;
}

/**
 * gst_play_set_audio_sink:
 * @play: a #GstPlay.
 * @audio_sink: a #GstElement.
 *
 * Set @audio_sink as the audio sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */

gboolean
gst_play_set_audio_sink (GstPlay * play, GstElement * audio_sink)
{
  g_object_set (G_OBJECT (play->priv->playbin), "audio-sink", audio_sink, NULL);

  return TRUE;
}

/**
 * gst_play_set_visualization:
 * @play: a #GstPlay.
 * @element: a #GstElement.
 *
 * Set @video_sink as the video sink element of @play.
 *
 * Returns: TRUE if call succeeded.
 */

gboolean
gst_play_set_visualization (GstPlay * play, GstElement * vis_element)
{
  /* unset old */
  if (play->priv->vis_element) {
    gst_object_unref (GST_OBJECT (play->priv->vis_element));
    play->priv->vis_element = NULL;
  }

  /* set new */
  if (vis_element) {
    gst_object_ref (GST_OBJECT (vis_element));
    play->priv->vis_element = vis_element;
  }

  /* use */
  if (play->priv->vis_element && play->priv->enable_vis) {
    g_object_set (G_OBJECT (play->priv->playbin),
        "vis-plugin", vis_element, NULL);
  } else {
    g_object_set (G_OBJECT (play->priv->playbin), "vis-plugin", NULL, NULL);
  }

  return TRUE;
}

/**
 * gst_play_connect_visualization:
 * @play: a #GstPlay.
 * @connect: a #gboolean indicating wether or not
 * visualization should be connected.
 *
 * Connect or disconnect visualization bin in @play.
 *
 * Returns: TRUE if call succeeded.
 */

gboolean
gst_play_connect_visualization (GstPlay * play, gboolean connect)
{
  play->priv->enable_vis = connect;

  /* use */
  if (play->priv->vis_element && play->priv->enable_vis) {
    g_object_set (G_OBJECT (play->priv->playbin),
        "vis-plugin", play->priv->vis_element, NULL);
  } else {
    g_object_set (G_OBJECT (play->priv->playbin), "vis-plugin", NULL, NULL);
  }

  return TRUE;
}

/**
 * gst_play_get_framerate:
 * @play: a #GstPlay.
 *
 * Get the video framerate from @play.
 *
 * Returns: a #gdouble indicating video framerate in frame per second.
 */

gdouble
gst_play_get_framerate (GstPlay * play)
{
  const GList *streaminfo = NULL;
  const GstStructure *str = NULL;
  gdouble value;

  g_object_get (G_OBJECT (play->priv->playbin),
      "stream-info", &streaminfo, NULL);

  for (; streaminfo != NULL; streaminfo = streaminfo->next) {
    GObject *info = streaminfo->data;
    gint type;
    GParamSpec *pspec;
    GEnumValue *val;

    g_object_get (info, "type", &type, NULL);
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (info), "type");
    val = g_enum_get_value (G_PARAM_SPEC_ENUM (pspec)->enum_class, type);

    if (strstr (val->value_name, "VIDEO")) {
      const GstCaps *caps = NULL;
      GstPad *pad = NULL;

      g_object_get (info, "object", &pad, NULL);
      g_assert (GST_IS_PAD (pad));
      pad = (GstPad *) GST_PAD_REALIZE (pad);
      caps = GST_PAD_CAPS (pad);
      str = gst_caps_get_structure (caps, 0);
      break;
    }
  }

  if (!str)
    return 0.;

  gst_structure_get_double (str, "framerate", &value);

  return value;
}

/**
 * gst_play_get_sink_element:
 * @play: a #GstPlay.
 * @element: a #GstElement.
 * @sink_type: a #GstPlaySinkType.
 *
 * Searches recursively for a sink #GstElement with
 * type @sink_type in @element which is supposed to be a #GstBin.
 *
 * Returns: the sink #GstElement of @element.
 */

/*
 * (Ronald) what is this?
 */

GstElement *
gst_play_get_sink_element (GstPlay * play,
    GstElement * element, GstPlaySinkType sink_type)
{
  GList *elements = NULL;
  const GList *pads = NULL;
  gboolean has_src, has_correct_type;

  g_return_val_if_fail (GST_IS_PLAY (play), NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  GST_DEBUG_OBJECT (play, "looking for sink element in %s",
      GST_ELEMENT_NAME (element));

  if (!GST_IS_BIN (element)) {
    /* since its not a bin, we'll assume this
     * element is a sink element */
    GST_DEBUG_OBJECT (play, "not a bin, returning %s as sink element",
        GST_ELEMENT_NAME (element));
    return element;
  }

  /* FIXME, not MT safe */
  elements = (GList *) GST_BIN (element)->children;

  /* traverse all elements looking for one without src pad */

  while (elements) {
    element = GST_ELEMENT (elements->data);
    GST_DEBUG_OBJECT (play, "looking at element %s",
        GST_ELEMENT_NAME (element));

    /* Recursivity :) */

    if (GST_IS_BIN (element)) {
      element = gst_play_get_sink_element (play, element, sink_type);
      if (GST_IS_ELEMENT (element))
        return element;
    } else {
      /* FIXME, not MT safe */
      pads = element->pads;
      has_src = FALSE;
      has_correct_type = FALSE;
      while (pads) {
        /* check for src pad */
        if (GST_PAD_DIRECTION (GST_PAD (pads->data)) == GST_PAD_SRC) {
          GST_DEBUG_OBJECT (play, "element %s has a src pad",
              GST_ELEMENT_NAME (element));
          has_src = TRUE;
          break;
        } else {
          /* If not a src pad checking caps */
          GstPad *pad;
          GstCaps *caps;
          GstStructure *structure;
          int i;
          gboolean has_video_cap = FALSE;
          gboolean has_audio_cap = FALSE;

          pad = GST_PAD (pads->data);
          caps = gst_pad_get_caps (pad);
          /* loop over all caps members to find mime types */
          for (i = 0; i < gst_caps_get_size (caps); ++i) {
            structure = gst_caps_get_structure (caps, i);

            GST_DEBUG_OBJECT (play,
                "looking at caps %d pad %s:%s on element %s with mime %s", i,
                GST_DEBUG_PAD_NAME (pad),
                GST_ELEMENT_NAME (element), gst_structure_get_name (structure));

            if (strcmp (gst_structure_get_name (structure),
                    "audio/x-raw-int") == 0) {
              has_audio_cap = TRUE;
            }

            if (strcmp (gst_structure_get_name (structure),
                    "video/x-raw-yuv") == 0 ||
                strcmp (gst_structure_get_name (structure),
                    "video/x-raw-rgb") == 0) {
              has_video_cap = TRUE;
            }
          }

          gst_caps_unref (caps);

          switch (sink_type) {
            case GST_PLAY_SINK_TYPE_AUDIO:
              if (has_audio_cap)
                has_correct_type = TRUE;
              break;
            case GST_PLAY_SINK_TYPE_VIDEO:
              if (has_video_cap)
                has_correct_type = TRUE;
              break;
            case GST_PLAY_SINK_TYPE_ANY:
              if ((has_video_cap) || (has_audio_cap))
                has_correct_type = TRUE;
              break;
            default:
              has_correct_type = FALSE;
          }
        }

        pads = g_list_next (pads);

      }

      if ((!has_src) && (has_correct_type)) {
        GST_DEBUG_OBJECT (play, "found %s with src pad and correct type",
            GST_ELEMENT_NAME (element));
        return element;
      }
    }

    elements = g_list_next (elements);
  }

  /* we didn't find a sink element */

  return NULL;
}

/**
 * gst_play_get_all_by_interface:
 * @play: a #GstPlay.
 * @interface: an interface.
 *
 * Returns all elements that are used by @play implementing the given interface.
 *
 * Returns: a #GList of #GstElement implementing the interface.
 *
 * Not MT safe.
 */

GList *
gst_play_get_all_by_interface (GstPlay * play, GType interface_type)
{
  GstElement *videosink = NULL, *audiosink = NULL;
  GList *res = NULL;
  GstIterator *it = NULL;

  g_object_get (G_OBJECT (play->priv->playbin),
      "video-sink", &videosink, "audio-sink", &audiosink, NULL);

  /* ehw... */
  if (videosink && GST_IS_BIN (videosink)) {
    it = gst_bin_iterate_all_by_interface (GST_BIN (videosink), interface_type);
  }
  if (!res && audiosink && GST_IS_BIN (audiosink)) {
    it = gst_bin_iterate_all_by_interface (GST_BIN (audiosink), interface_type);
  }
  if (it != NULL) {
    gpointer data;

    while (gst_iterator_next (it, &data) == GST_ITERATOR_OK) {
      res = g_list_prepend (res, data);
    }
  }

  return res;
}

/**
 * gst_play_new:
 * @error: a pointer to a #GError, which will be filled in on error.
 *
 * Creates a new GstPlay object.
 *
 * Returns: a newly created #GstPlay, or NULL on error (the #GError
 *          will be set accordingly).
 */

GstPlay *
gst_play_new (GError ** error)
{
  GstPlay *play = g_object_new (GST_TYPE_PLAY, NULL);

  if (!play->priv->playbin) {
    g_set_error (error, 0, 0, _("Failed to create playbin object"));
    g_object_unref (G_OBJECT (play));

    return NULL;
  }

  return play;
}

/* =========================================== */
/*                                             */
/*          Object typing & Creation           */
/*                                             */
/* =========================================== */

GType
gst_play_get_type (void)
{
  static GType play_type = 0;

  if (!play_type) {
    static const GTypeInfo play_info = {
      sizeof (GstPlayClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_class_init,
      NULL,
      NULL,
      sizeof (GstPlay),
      0,
      (GInstanceInitFunc) gst_play_init,
      NULL
    };

    play_type = g_type_register_static (GST_TYPE_PIPELINE, "GstPlay",
        &play_info, 0);
  }

  return play_type;
}
