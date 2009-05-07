/* demo-player.c
 * Copyright (C) 2008 Rov Juvano <rovjuvano@users.sourceforge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "demo-player.h"
#include "gst/gst.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "demo-player"

enum
{
  SIGNAL_ERROR,
  SIGNAL_RATE_CHANGE,
  SIGNAL_PLAYING_STARTED,
  SIGNAL_PLAYING_PAUSED,
  SIGNAL_PLAYING_ENDED,
  LAST_SIGNAL
};
static guint demo_player_signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_0,
  PROP_RATE,
  PROP_STRIDE,
  PROP_OVERLAP,
  PROP_SEARCH,
  PROP_DISABLED
};

typedef struct _DemoPlayerPrivate
{
  gdouble rate;
  GstElement *scaletempo;
  GstElement *pipeline;
  gboolean is_disabled;
  GstElement *scaletempo_line;
  GstElement *scalerate_line;
  gboolean ignore_state_change;
} DemoPlayerPrivate;

#define DEMO_PLAYER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEMO_TYPE_PLAYER, DemoPlayerPrivate))


static gboolean
no_pipeline (DemoPlayer * player)
{
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  if (!priv->pipeline) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "No media loaded");
    return TRUE;
  }
  return FALSE;
}

static gboolean
demo_player_event_listener (GstElement * host, GstEvent * event, gpointer data)
{
  DemoPlayer *player = DEMO_PLAYER (data);
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);

  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    gdouble rate, applied_rate;
    gdouble new_rate;

    gst_event_parse_new_segment_full (event, NULL, &rate, &applied_rate, NULL,
        NULL, NULL, NULL);
    new_rate = rate * applied_rate;
    if (priv->rate != new_rate) {
      priv->rate = new_rate;
      g_signal_emit (player, demo_player_signals[SIGNAL_RATE_CHANGE], 0,
          new_rate);
    }
  }

  return TRUE;
}

static void
demo_player_state_changed_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  DemoPlayer *player = DEMO_PLAYER (data);
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  GstState old, new, pending;

  if (GST_ELEMENT (GST_MESSAGE_SRC (message)) != priv->pipeline)
    return;

  gst_message_parse_state_changed (message, &old, &new, &pending);

  if (pending == GST_STATE_VOID_PENDING) {
    if (priv->ignore_state_change) {
      priv->ignore_state_change = FALSE;
    } else if (new == GST_STATE_PAUSED) {
      g_signal_emit (player, demo_player_signals[SIGNAL_PLAYING_PAUSED], 0);
    } else if (new == GST_STATE_PLAYING) {
      g_signal_emit (player, demo_player_signals[SIGNAL_PLAYING_STARTED], 0);
    }
  }
}

static void
demo_player_eos_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  DemoPlayer *player = DEMO_PLAYER (data);
  g_signal_emit (player, demo_player_signals[SIGNAL_PLAYING_ENDED], 0);
}

#define MAKE_ELEMENT(line, var, type, name)                         \
  if ( !(var = gst_element_factory_make (type, name) ) ) {          \
    g_print ("element could not be created: %s/%s\n", type, name);  \
    return;                                                         \
  }                                                                 \
  if (line) gst_bin_add (GST_BIN (line), var);

#define LINK_ELEMENTS(src, sink)                            \
  if (!gst_element_link (src, sink)) {                      \
    g_warning ("Failed to link elements: %s -> %s",         \
        GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink) );  \
    return;                                                 \
  }

static void
demo_player_build_pipeline (DemoPlayer * player)
{
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  GstElement *filter, *playbin, *vsink, *audioline, *format, *resample, *asink;
  GstPlugin *gconf;
  GstBus *bus;
  gboolean has_gconf;
  const gchar *audiosink_name;
  GstPad *ghostpad;

  priv->pipeline = NULL;
  if (!priv->scaletempo) {
    return;
  }

  filter = priv->scaletempo;

  MAKE_ELEMENT (NULL, playbin, "playbin", "playbin");

  gconf = gst_default_registry_find_plugin ("gconfelements");
  has_gconf = (gconf != NULL);
  gst_object_unref (gconf);

  if (has_gconf) {
    MAKE_ELEMENT (NULL, vsink, "gconfvideosink", "vsink");
    g_object_set (G_OBJECT (playbin), "video_sink", vsink, NULL);
  }
  audiosink_name = has_gconf ? "gconfaudiosink" : "autoaudiosink";

  audioline = gst_bin_new ("audioline");
  gst_bin_add (GST_BIN (audioline), filter);
  MAKE_ELEMENT (audioline, format, "audioconvert", "format");
  MAKE_ELEMENT (audioline, resample, "audioresample", "resample");
  MAKE_ELEMENT (audioline, asink, audiosink_name, "audio_sink");
  LINK_ELEMENTS (filter, format);
  LINK_ELEMENTS (format, resample);
  LINK_ELEMENTS (resample, asink);

  gst_pad_add_event_probe (gst_element_get_static_pad (asink, "sink"),
      G_CALLBACK (demo_player_event_listener), player);

  ghostpad = gst_element_get_static_pad (filter, "sink");
  gst_element_add_pad (audioline, gst_ghost_pad_new ("sink", ghostpad));
  gst_object_unref (ghostpad);
  g_object_set (G_OBJECT (playbin), "audio-sink", audioline, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (playbin));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (demo_player_state_changed_cb), player);
  g_signal_connect (bus, "message::eos", G_CALLBACK (demo_player_eos_cb),
      player);
  gst_object_unref (bus);

  priv->scaletempo = filter;
  priv->pipeline = playbin;

  priv->scaletempo_line = audioline;
  MAKE_ELEMENT (NULL, priv->scalerate_line, audiosink_name,
      "scaling_audio_sink");
  gst_pad_add_event_probe (gst_element_get_static_pad (priv->scalerate_line,
          "sink"), G_CALLBACK (demo_player_event_listener), player);
  g_object_ref (priv->scaletempo_line);
  g_object_ref (priv->scalerate_line);
}


/* method implementations */
static void
_set_rate (DemoPlayer * player, gdouble new_rate, gint second)
{
  DemoPlayerPrivate *priv;
  gint64 pos;
  GstSeekType seek_type;


  if (new_rate == 0) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "Cannot set playback to zero.  Pausing instead.");
    demo_player_pause (player);
  }

  priv = DEMO_PLAYER_GET_PRIVATE (player);

  if (second < 0) {
    GstFormat fmt = GST_FORMAT_TIME;
    seek_type = GST_SEEK_TYPE_SET;
    if (!gst_element_query_position (priv->pipeline, &fmt, &pos)) {
      // This should be the default but too many upstream elements seek anyway
      pos = GST_CLOCK_TIME_NONE;
      seek_type = GST_SEEK_TYPE_NONE;
    }
  } else {
    seek_type = GST_SEEK_TYPE_SET;
    pos = second * GST_SECOND;
  }

  if (!gst_element_seek (priv->pipeline, new_rate,
          GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
          seek_type, pos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "Unable to change playback rate");
  } else {
    priv->ignore_state_change = TRUE;
  }
}

static void
demo_player_scale_rate_func (DemoPlayer * player, gdouble scale)
{
  DemoPlayerPrivate *priv;
  if (no_pipeline (player))
    return;

  priv = DEMO_PLAYER_GET_PRIVATE (player);

  if (scale != 1.0) {
    g_message ("Scaling Rate by: %3.2f", scale);
    _set_rate (player, priv->rate * scale, -1);
  }
}

static void
demo_player_set_rate_func (DemoPlayer * player, gdouble new_rate)
{
  DemoPlayerPrivate *priv;

  if (no_pipeline (player))
    return;

  priv = DEMO_PLAYER_GET_PRIVATE (player);

  if (priv->rate != new_rate) {
    g_message ("Setting Rate to: %3.2f", new_rate);
    _set_rate (player, new_rate, -1);
  }
}

static gboolean
_set_state_and_wait (DemoPlayer * player,
    GstState new_state, GstClockTime timeout, const gchar * error_msg)
{
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  GstStateChangeReturn ret = gst_element_set_state (priv->pipeline, new_state);
  if (ret == GST_STATE_CHANGE_ASYNC) {
    ret = gst_element_get_state (priv->pipeline, NULL, NULL, timeout);
  }
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0, error_msg);
    return FALSE;
  }
  return TRUE;
}

static void
demo_player_load_uri_func (DemoPlayer * player, gchar * uri)
{
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  GstState end_state;
  gdouble rate;

  if (!priv->pipeline) {
    demo_player_build_pipeline (player);
    if (!priv->pipeline) {
      g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
          "Could not build player");
      return;
    }
  }
  if (!g_str_has_prefix (uri, "file:///")) {
    GError *err = NULL;
    if (g_path_is_absolute (uri)) {
      uri = g_filename_to_uri (uri, NULL, &err);
    } else {
      gchar *curdir = g_get_current_dir ();
      gchar *absolute_path = g_strconcat (curdir, G_DIR_SEPARATOR_S, uri, NULL);
      uri = g_filename_to_uri (absolute_path, NULL, &err);
      g_free (absolute_path);
      g_free (curdir);
    }
    if (err) {
      gchar *msg = g_strconcat ("Could not load uri: ", err->message, NULL);
      g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0, msg);
      return;
    }
  }

  g_message ("Loading URI: %s", uri);

  end_state =
      (GST_STATE (priv->pipeline) ==
      GST_STATE_PLAYING) ? GST_STATE_PLAYING : GST_STATE_PAUSED;
  if (!_set_state_and_wait (player, GST_STATE_NULL, 10 * GST_SECOND,
          "Unable to load uri"))
    return;

  g_object_set (G_OBJECT (priv->pipeline), "uri", uri, NULL);

  rate = priv->rate;
  if (rate && rate != 1.0) {
    _set_state_and_wait (player, GST_STATE_PAUSED, 10 * GST_SECOND,
        "Unable to keep playback rate");
    _set_rate (player, rate, -1);
  }

  gst_element_set_state (priv->pipeline, end_state);
}

static void
demo_player_play_func (DemoPlayer * player)
{
  DemoPlayerPrivate *priv;
  GstStateChangeReturn ret;

  if (no_pipeline (player))
    return;

  priv = DEMO_PLAYER_GET_PRIVATE (player);

  if (GST_STATE (priv->pipeline) == GST_STATE_PLAYING) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "Already playing");
    return;
  }

  g_debug ("Starting to Play");
  ret = gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "Unable to start playback");
    return;
  }
}

static void
demo_player_pause_func (DemoPlayer * player)
{
  DemoPlayerPrivate *priv;
  GstStateChangeReturn ret;

  if (no_pipeline (player))
    return;

  priv = DEMO_PLAYER_GET_PRIVATE (player);

  if (GST_STATE (priv->pipeline) == GST_STATE_PAUSED) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "Already paused");
    return;
  }

  g_debug ("Starting to Pause");
  ret = gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "Unable to pause playback");
    return;
  }
}

static void
_seek_to (DemoPlayer * player, gint new_second)
{
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  if (!gst_element_seek (priv->pipeline, priv->rate,
          GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
          GST_SEEK_TYPE_SET, new_second * GST_SECOND,
          GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0, "Seek failed");
    return;
  }

  priv->ignore_state_change = TRUE;
}

static void
demo_player_seek_by_func (DemoPlayer * player, gint seconds)
{
  gint pos;

  if (no_pipeline (player))
    return;

  g_debug ("Seeking by: %i", seconds);

  pos = demo_player_get_position (player);
  if (pos < 0) {
    g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
        "Seek-by failed: could not determine position");
    return;
  }

  _seek_to (player, MAX (0, pos + seconds));
}

static void
demo_player_seek_to_func (DemoPlayer * player, gint second)
{
  gint new_second;

  if (no_pipeline (player))
    return;

  g_debug ("Seeking to: %i", second);

  if (second < 0) {
    gint dur = demo_player_get_duration (player);
    if (dur < 0) {
      g_signal_emit (player, demo_player_signals[SIGNAL_ERROR], 0,
          "Seek-to failed: could not determine duration");
      return;
    }
    new_second = MAX (0, dur + second);
  } else {
    new_second = second;
  }

  _seek_to (player, new_second);
}

static gint
demo_player_get_position_func (DemoPlayer * player)
{
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  gint64 pos;
  GstFormat fmt = GST_FORMAT_TIME;

  if (!priv->pipeline)
    return -1;

  if (!gst_element_query_position (priv->pipeline, &fmt, &pos) || pos < 0) {
    return -1;
  }

  return (gint) (pos / GST_SECOND);
}

static gint
demo_player_get_duration_func (DemoPlayer * player)
{
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  gint64 dur;
  GstFormat fmt = GST_FORMAT_TIME;

  if (!priv->pipeline)
    return -1;

  if (!gst_element_query_duration (priv->pipeline, &fmt, &dur) || dur < 0) {
    return -1;
  }

  return (gint) (dur / GST_SECOND);
}


/* Method wrappers */
void
demo_player_scale_rate (DemoPlayer * player, gdouble scale)
{
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_PLAYER_GET_CLASS (player)->scale_rate (player, scale);
}

void
demo_player_set_rate (DemoPlayer * player, gdouble new_rate)
{
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_PLAYER_GET_CLASS (player)->set_rate (player, new_rate);
}

void
demo_player_load_uri (DemoPlayer * player, gchar * uri)
{
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_PLAYER_GET_CLASS (player)->load_uri (player, uri);
}

void
demo_player_play (DemoPlayer * player)
{
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_PLAYER_GET_CLASS (player)->play (player);
}

void
demo_player_pause (DemoPlayer * player)
{
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_PLAYER_GET_CLASS (player)->pause (player);
}

void
demo_player_seek_by (DemoPlayer * player, gint seconds)
{
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_PLAYER_GET_CLASS (player)->seek_by (player, seconds);
}

void
demo_player_seek_to (DemoPlayer * player, gint second)
{
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_PLAYER_GET_CLASS (player)->seek_to (player, second);
}

gint
demo_player_get_position (DemoPlayer * player)
{
  g_return_val_if_fail (DEMO_IS_PLAYER (player), -1);

  return DEMO_PLAYER_GET_CLASS (player)->get_position (player);
}

gint
demo_player_get_duration (DemoPlayer * player)
{
  g_return_val_if_fail (DEMO_IS_PLAYER (player), -1);

  return DEMO_PLAYER_GET_CLASS (player)->get_duration (player);
}

/* GObject overrides */
static void
demo_player_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  DemoPlayer *player = DEMO_PLAYER (object);
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  switch (property_id) {
    case PROP_RATE:
      g_value_set_double (value, priv->rate);
      break;
    case PROP_STRIDE:
      g_object_get_property (G_OBJECT (priv->scaletempo), "stride", value);
      break;
    case PROP_OVERLAP:
      g_object_get_property (G_OBJECT (priv->scaletempo), "overlap", value);
      break;
    case PROP_SEARCH:
      g_object_get_property (G_OBJECT (priv->scaletempo), "search", value);
      break;
    case PROP_DISABLED:
      g_value_set_boolean (value, priv->is_disabled);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
demo_player_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  DemoPlayer *player = DEMO_PLAYER (object);
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  switch (property_id) {
    case PROP_STRIDE:
      g_object_set_property (G_OBJECT (priv->scaletempo), "stride", value);
      break;
    case PROP_OVERLAP:
      g_object_set_property (G_OBJECT (priv->scaletempo), "overlap", value);
      break;
    case PROP_SEARCH:
      g_object_set_property (G_OBJECT (priv->scaletempo), "search", value);
      break;
    case PROP_DISABLED:{
      gdouble rate = priv->rate;
      gint pos = demo_player_get_position (player);
      GstState end_state;
      GstElement *new_sink;

      priv->is_disabled = g_value_get_boolean (value);

      g_debug ("Scaletempo: %s", priv->is_disabled ? "disabled" : "enabled");

      end_state =
          (GST_STATE (priv->pipeline) ==
          GST_STATE_PLAYING) ? GST_STATE_PLAYING : GST_STATE_PAUSED;
      if (!_set_state_and_wait (player, GST_STATE_NULL, 10 * GST_SECOND,
              "Unable to disable"))
        break;

      new_sink =
          (priv->is_disabled) ? priv->scalerate_line : priv->scaletempo_line;
      g_object_set (G_OBJECT (priv->pipeline), "audio-sink", new_sink, NULL);

      if (pos > 0 || (rate && rate != 1.0)) {
        _set_state_and_wait (player, GST_STATE_PAUSED, 10 * GST_SECOND,
            "Unable to keep playback position and rate");
        _set_rate (player, rate, pos);
      }

      gst_element_set_state (priv->pipeline, end_state);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


/* GTypeInfo functions */
static void
demo_player_init (GTypeInstance * instance, gpointer klass)
{
  DemoPlayer *player = (DemoPlayer *) instance;
  DemoPlayerPrivate *priv = DEMO_PLAYER_GET_PRIVATE (player);
  priv->scaletempo = gst_element_factory_make ("scaletempo", "scaletempo");
  if (!priv->scaletempo) {
    g_error ("Unable to make scaletempo element.");
  }
  priv->rate = 1.0;
  priv->pipeline = NULL;
  priv->ignore_state_change = FALSE;
  priv->is_disabled = FALSE;
}

static void
demo_player_class_init (gpointer klass, gpointer class_data)
{
  DemoPlayerClass *player_class = (DemoPlayerClass *) klass;
  GObjectClass *as_object_class = G_OBJECT_CLASS (klass);
  GType type;

  g_type_class_add_private (klass, sizeof (DemoPlayerPrivate));

  /* DemoPlayer */
  player_class->scale_rate = demo_player_scale_rate_func;
  player_class->set_rate = demo_player_set_rate_func;
  player_class->load_uri = demo_player_load_uri_func;
  player_class->play = demo_player_play_func;
  player_class->pause = demo_player_pause_func;
  player_class->seek_by = demo_player_seek_by_func;
  player_class->seek_to = demo_player_seek_to_func;
  player_class->get_position = demo_player_get_position_func;
  player_class->get_duration = demo_player_get_duration_func;

  /* GObject */
  as_object_class->get_property = demo_player_get_property;
  as_object_class->set_property = demo_player_set_property;

  /* Properties */
  g_object_class_install_property (as_object_class, PROP_RATE,
      g_param_spec_double ("rate", "Rate", "Current playback rate",
          -128, 128, 1.0, G_PARAM_READABLE));

  g_object_class_install_property (as_object_class, PROP_STRIDE,
      g_param_spec_uint ("stride", "Stride Length",
          "Length in milliseconds to output each stride", 1, 10000, 60,
          G_PARAM_READWRITE));

  g_object_class_install_property (as_object_class, PROP_OVERLAP,
      g_param_spec_double ("overlap", "Overlap Length",
          "Percentage of stride to overlap", 0, 1, .2, G_PARAM_READWRITE));

  g_object_class_install_property (as_object_class, PROP_SEARCH,
      g_param_spec_uint ("search", "Search Length",
          "Length in milliseconds to search for best overlap position", 0,
          10000, 14, G_PARAM_READWRITE));

  g_object_class_install_property (as_object_class, PROP_DISABLED,
      g_param_spec_boolean ("disabled", "disable scaletempo",
          "Disable scaletempo and scale bothe tempo and pitch", FALSE,
          G_PARAM_READWRITE));

  /* Signals */
  type = G_TYPE_FROM_CLASS (klass);
  demo_player_signals[SIGNAL_ERROR] = g_signal_new ("error", type,
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING);

  demo_player_signals[SIGNAL_RATE_CHANGE] = g_signal_new ("rate-changed", type,
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__DOUBLE, G_TYPE_NONE, 1, G_TYPE_DOUBLE);

  demo_player_signals[SIGNAL_PLAYING_STARTED] =
      g_signal_new ("playing-started", type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  demo_player_signals[SIGNAL_PLAYING_PAUSED] =
      g_signal_new ("playing-paused", type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  demo_player_signals[SIGNAL_PLAYING_ENDED] =
      g_signal_new ("playing-ended", type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

GType
demo_player_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0)) {
    static const GTypeInfo info = {
      sizeof /* Class */ (DemoPlayerClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) demo_player_class_init,
      (GClassFinalizeFunc) NULL,
      (gconstpointer) NULL,     /* class_data */
      sizeof /* Instance */ (DemoPlayer),
      /* n_preallocs */ 0,
      (GInstanceInitFunc) demo_player_init,
      (const GTypeValueTable *) NULL
    };
    type = g_type_register_static (G_TYPE_OBJECT, "DemoPlayer", &info, 0);
  }
  return type;
}
