/* demo-gui.c
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gprintf.h>
#include <math.h>
#include "demo-gui.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "demo-gui"

enum
{
  SIGNAL_ERROR,
  SIGNAL_QUITING,
  LAST_SIGNAL
};
static guint demo_gui_signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_0,
};

typedef struct _DemoGuiPrivate
{
  DemoPlayer *player;
  GList *uris;
  GList *now_playing;
  gboolean is_playing;
  GtkWidget *window;
  GtkEntry *rate_entry;
  GtkStatusbar *status_bar;
  gint position_updater_id;
  GtkRange *seek_range;
  GtkLabel *amount_played;
  GtkLabel *amount_to_play;
  GtkAction *play_action;
  GtkAction *pause_action;
  GtkAction *open_file;
  GtkAction *playlist_next;
} DemoGuiPrivate;

#define DEMO_GUI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEMO_TYPE_GUI, DemoGuiPrivate))

/* forward declarations */
static GValueArray *build_gvalue_array (guint n_values, ...);

/* Handlers for status bar and seek bar */
static int
pop_status_bar (gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  GtkStatusbar *sb =
      GTK_STATUSBAR (g_value_get_object (g_value_array_get_nth (gvalues, 0)));
  guint msg_id = g_value_get_uint (g_value_array_get_nth (gvalues, 1));

  gtk_statusbar_remove (sb, 0, msg_id);
  return FALSE;
}

#define DEFAULT_STATUS_BAR_TIMEOUT 2
static void
status_bar_printf (GtkStatusbar * sb, guint seconds, gchar const *format, ...)
{
  va_list args;
  gchar msg[80];
  guint msg_id;

  va_start (args, format);
  g_vsnprintf (msg, 80, format, args);
  va_end (args);

  msg_id = gtk_statusbar_push (sb, 0, msg);
  g_timeout_add (2000, pop_status_bar,
      build_gvalue_array (2, G_TYPE_OBJECT, sb, G_TYPE_UINT, msg_id));
}

#define PRINTF_TIME_FORMAT "u:%02u:%02u"
#define PRINTF_TIME_ARGS(t)                       \
    (t >= 0) ? (guint) ((t) / (60 * 60)) : 99,    \
    (t >= 0) ? (guint) (((t) / (60)) % 60) : 99,  \
    (t >= 0) ? (guint) ((t) % 60) : 99

static gchar *
demo_gui_seek_bar_format (GtkScale * scale, gdouble value, gpointer data)
{
  return g_strdup_printf ("%" PRINTF_TIME_FORMAT,
      PRINTF_TIME_ARGS ((gint64) value));
}

static gboolean
update_position (gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gchar pos_str[16], dur_str[16];
  gint pos = demo_player_get_position (priv->player);

  if (pos > 0) {
    gint dur = demo_player_get_duration (priv->player);

    g_snprintf (pos_str, 16, "%" PRINTF_TIME_FORMAT, PRINTF_TIME_ARGS (pos));
    if (dur > 0) {
      g_snprintf (dur_str, 16, "-%" PRINTF_TIME_FORMAT,
          PRINTF_TIME_ARGS (dur - pos));
    } else {
      dur = pos;
      g_sprintf (dur_str, "-??:??:??");
    }
    if (dur > 0)
      gtk_range_set_range (GTK_RANGE (priv->seek_range), 0, (gdouble) dur);
    gtk_range_set_value (GTK_RANGE (priv->seek_range), (gdouble) pos);
  } else {
    g_sprintf (pos_str, "??:??:??");
    g_sprintf (dur_str, "-??:??:??");
  }
  gtk_label_set_text (GTK_LABEL (priv->amount_played), pos_str);
  gtk_label_set_text (GTK_LABEL (priv->amount_to_play), dur_str);

  return priv->is_playing;
}


static gboolean
demo_gui_seek_bar_change (GtkRange * range,
    GtkScrollType scroll, gdouble value, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gint new_second = (gint) value;

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Seeking to %i second", new_second);
  demo_player_seek_to (priv->player, new_second);

  return FALSE;
}


/* Callbacks for actions */
static void
demo_gui_do_change_rate (GtkAction * action, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui = g_value_get_object (g_value_array_get_nth (gvalues, 0));
  gdouble scale_amount =
      g_value_get_double (g_value_array_get_nth (gvalues, 1));
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Changing rate by %3.2lf", scale_amount);

  demo_player_scale_rate (priv->player, scale_amount);
}

static void
demo_gui_do_set_rate (GtkAction * action, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui = g_value_get_object (g_value_array_get_nth (gvalues, 0));
  gdouble new_rate = g_value_get_double (g_value_array_get_nth (gvalues, 1));
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Setting rate to %3.2lf", new_rate);

  demo_player_set_rate (priv->player, new_rate);
}

static gboolean
demo_gui_do_rate_entered (GtkWidget * widget, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gchar *err = NULL;
  const gchar *text = gtk_entry_get_text (GTK_ENTRY (widget));
  double new_rate = g_strtod (text, &err);

  if (*err) {
    gtk_widget_error_bell (priv->window);
    status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
        "Invalid rate: %s", text);
    return TRUE;
  }

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Setting rate to %3.2lf", new_rate);

  demo_player_set_rate (priv->player, new_rate);
  return FALSE;
}

static void
demo_gui_do_toggle_advanced (GtkAction * action, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui =
      DEMO_GUI (g_value_get_object (g_value_array_get_nth (gvalues, 0)));
  GtkWidget *stride_ui =
      GTK_WIDGET (g_value_get_object (g_value_array_get_nth (gvalues, 1)));
  GtkWidget *overlap_ui =
      GTK_WIDGET (g_value_get_object (g_value_array_get_nth (gvalues, 2)));
  GtkWidget *search_ui =
      GTK_WIDGET (g_value_get_object (g_value_array_get_nth (gvalues, 3)));
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gboolean active;

  status_bar_printf (priv->status_bar, 1, "Toggling advanced mode");

  active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
  gtk_widget_set_sensitive (stride_ui, active);
  gtk_widget_set_sensitive (overlap_ui, active);
  gtk_widget_set_sensitive (search_ui, active);
}

static void
demo_gui_do_toggle_disabled (GtkAction * action, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui =
      DEMO_GUI (g_value_get_object (g_value_array_get_nth (gvalues, 0)));
  GtkAction *advanced_action =
      GTK_ACTION (g_value_get_object (g_value_array_get_nth (gvalues, 1)));
  GtkWidget *advanced_ui =
      GTK_WIDGET (g_value_get_object (g_value_array_get_nth (gvalues, 2)));

  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gboolean active;

  status_bar_printf (priv->status_bar, 1, "Toggling disabled");

  active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
  gtk_action_set_sensitive (GTK_ACTION (advanced_action), !active);
  gtk_widget_set_sensitive (GTK_WIDGET (advanced_ui), !active);
  g_object_set (G_OBJECT (priv->player), "disabled", active, NULL);
}

static void
demo_gui_do_seek (GtkAction * action, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui =
      DEMO_GUI (g_value_get_object (g_value_array_get_nth (gvalues, 0)));
  gint seconds = g_value_get_int (g_value_array_get_nth (gvalues, 1));
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Requesting seek by %i seconds", seconds);

  demo_player_seek_by (priv->player, seconds);
}

static void
demo_gui_do_play (GtkAction * action, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  if (priv->is_playing) {
    g_signal_emit (gui, demo_gui_signals[SIGNAL_ERROR], 0, "Already playing");
    return;
  }

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Requesting playback start");

  demo_player_play (priv->player);
}

static void
demo_gui_do_pause (GtkAction * action, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  if (!priv->is_playing) {
    g_signal_emit (gui, demo_gui_signals[SIGNAL_ERROR], 0, "Already paused");
    return;
  }

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Requesting playback pause");

  demo_player_pause (priv->player);
}

static void
demo_gui_do_play_pause (GtkAction * action, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Reqesting playback toggle");

  if (priv->is_playing)
    gtk_action_activate (priv->pause_action);
  else
    gtk_action_activate (priv->play_action);
}

static void
demo_gui_do_open_file (GtkAction * action, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
      GTK_WINDOW (priv->window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
      NULL);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
    char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
    GError *err = NULL;

    g_list_free (priv->uris);
    priv->uris = NULL;
    priv->now_playing = NULL;
    demo_player_load_uri (priv->player, g_filename_to_uri (filename, NULL,
            &err));
    g_free (filename);
  }
  gtk_widget_destroy (dialog);
}

static void
demo_gui_do_playlist_prev (GtkAction * action, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  if (priv->now_playing) {
    if (priv->now_playing->prev) {
      status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
          "Playlist previous");
      priv->now_playing = priv->now_playing->prev;
    } else {
      priv->now_playing = NULL;
      gtk_widget_error_bell (priv->window);
      status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
          "Beginning of playlist");
      return;
    }
  } else if (priv->uris) {
    status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
        "Playlist previous: wrap");
    priv->now_playing = g_list_last (priv->uris);
  } else {
    gtk_action_activate (priv->open_file);
    return;
  }

  demo_player_load_uri (priv->player, priv->now_playing->data);
}

static void
demo_gui_do_playlist_next (GtkAction * action, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  if (priv->now_playing) {
    if (priv->now_playing->next) {
      status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
          "Playlist next");
      priv->now_playing = priv->now_playing->next;
    } else {
      priv->now_playing = NULL;
      gtk_widget_error_bell (priv->window);
      status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
          "End of playlist");
      return;
    }
  } else if (priv->uris) {
    status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
        "Playlist next: wrap");
    priv->now_playing = priv->uris;
  } else {
    gtk_action_activate (priv->open_file);
    return;
  }

  demo_player_load_uri (priv->player, priv->now_playing->data);
}

static void
demo_gui_do_about_dialog (GtkAction * action, gpointer data)
{
  static const gchar *authors[] =
      { "Rov Juvano <rovjuvano@users.sourceforge.net>", NULL };

  gtk_show_about_dialog (NULL,
      "program-name", "gst-scaletempo-demo",
      "version", VERSION,
      "authors", authors,
      "license", "This program is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
This program is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
GNU General Public License for more details.\n\
\n\
You should have received a copy of the GNU General Public License\n\
along with this program.  If not, see <http://www.gnu.org/licenses/>.", "title", "About gst-scaletempo-demo", NULL);
}

static void
demo_gui_do_quit (gpointer source, gpointer data)
{
  gtk_main_quit ();
  g_signal_emit (DEMO_GUI (data), demo_gui_signals[SIGNAL_QUITING], 0, NULL);
}

static gboolean
demo_gui_request_set_stride (GtkSpinButton * spinbutton, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  guint new_stride = gtk_spin_button_get_value_as_int (spinbutton);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Reqesting setting stride to %u ms", new_stride);
  g_object_set (G_OBJECT (priv->player), "stride", new_stride, NULL);
  return TRUE;
}

static gboolean
demo_gui_request_set_overlap (GtkSpinButton * spinbutton, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gdouble new_overlap = gtk_spin_button_get_value_as_int (spinbutton);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Requesting setting overlap to %2.0lf%%", new_overlap);
  g_object_set (G_OBJECT (priv->player), "overlap", new_overlap / 100.0, NULL);
  return TRUE;
}

static gboolean
demo_gui_request_set_search (GtkSpinButton * spinbutton, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  guint new_search = gtk_spin_button_get_value_as_int (spinbutton);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Requesting setting search to %u ms", new_search);
  g_object_set (G_OBJECT (priv->player), "search", new_search, NULL);
  return TRUE;
}


/* Callbacks from signals */
static void
demo_gui_rate_changed (DemoPlayer * player, gdouble new_rate, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gchar e[6];

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Rate changed to %3.2lf", new_rate);

  g_snprintf (e, 6, "%3.2f", new_rate);
  gtk_entry_set_text (GTK_ENTRY (priv->rate_entry), e);
}

static void
demo_gui_playing_started (DemoPlayer * player, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  priv->is_playing = TRUE;
  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Playing started");

  gtk_action_set_sensitive (priv->play_action, FALSE);
  gtk_action_set_sensitive (priv->pause_action, TRUE);
  gtk_action_set_visible (priv->play_action, FALSE);
  gtk_action_set_visible (priv->pause_action, TRUE);

  if (priv->position_updater_id) {
    g_source_remove (priv->position_updater_id);
    priv->position_updater_id = 0;
  }
  update_position (gui);
  priv->position_updater_id = g_timeout_add (1000, update_position, gui);
}

static void
demo_gui_playing_paused (DemoPlayer * player, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  gtk_action_set_sensitive (priv->play_action, TRUE);
  gtk_action_set_sensitive (priv->pause_action, FALSE);
  gtk_action_set_visible (priv->play_action, TRUE);
  gtk_action_set_visible (priv->pause_action, FALSE);

  priv->is_playing = FALSE;

  if (priv->position_updater_id)
    g_source_remove (priv->position_updater_id);
  priv->position_updater_id = 0;
  update_position (gui);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Playing paused");
}

static void
demo_gui_playing_ended (DemoPlayer * player, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Playing ended");
  gtk_action_activate (priv->playlist_next);
}

static void
demo_gui_player_errored (DemoPlayer * player, const gchar * msg, gpointer data)
{
  DemoGui *gui = DEMO_GUI (data);
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  status_bar_printf (priv->status_bar, 5, msg);
}

static void
demo_gui_stride_changed (DemoPlayer * player, GParamSpec * pspec, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui =
      DEMO_GUI (g_value_get_object (g_value_array_get_nth (gvalues, 0)));
  GtkEntry *entry =
      GTK_ENTRY (g_value_get_object (g_value_array_get_nth (gvalues, 1)));
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  guint new_stride;
  gchar e[6];

  g_object_get (G_OBJECT (player), "stride", &new_stride, NULL);
  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Stride changed to %u", new_stride);

  snprintf (e, 6, "%u", new_stride);
  gtk_entry_set_text (entry, e);
}

static void
demo_gui_overlap_changed (DemoPlayer * player,
    GParamSpec * pspec, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui =
      DEMO_GUI (g_value_get_object (g_value_array_get_nth (gvalues, 0)));
  GtkEntry *entry =
      GTK_ENTRY (g_value_get_object (g_value_array_get_nth (gvalues, 1)));
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  gdouble new_overlap;
  gchar e[6];

  g_object_get (G_OBJECT (player), "overlap", &new_overlap, NULL);
  new_overlap *= 100;
  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Overlap changed to %2.0lf%%", new_overlap);

  snprintf (e, 6, "%2.0f", new_overlap);
  gtk_entry_set_text (entry, e);
}

static void
demo_gui_search_changed (DemoPlayer * player, GParamSpec * pspec, gpointer data)
{
  GValueArray *gvalues = (GValueArray *) data;
  DemoGui *gui =
      DEMO_GUI (g_value_get_object (g_value_array_get_nth (gvalues, 0)));
  GtkEntry *entry =
      GTK_ENTRY (g_value_get_object (g_value_array_get_nth (gvalues, 1)));
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  guint new_search;
  gchar e[6];

  g_object_get (G_OBJECT (player), "search", &new_search, NULL);
  status_bar_printf (priv->status_bar, DEFAULT_STATUS_BAR_TIMEOUT,
      "Search changed to %u", new_search);

  snprintf (e, 6, "%u", new_search);
  gtk_entry_set_text (entry, e);
}


/* method implementations */
static void
demo_gui_set_player_func (DemoGui * gui, DemoPlayer * player)
{
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);

  if (priv->player) {
    g_signal_handlers_disconnect_by_func (G_OBJECT (priv->player),
        G_CALLBACK (demo_gui_rate_changed), gui);
    g_signal_handlers_disconnect_by_func (G_OBJECT (priv->player),
        G_CALLBACK (demo_gui_playing_started), gui);
    g_signal_handlers_disconnect_by_func (G_OBJECT (priv->player),
        G_CALLBACK (demo_gui_playing_paused), gui);
    g_signal_handlers_disconnect_by_func (G_OBJECT (priv->player),
        G_CALLBACK (demo_gui_playing_ended), gui);
    g_signal_handlers_disconnect_by_func (G_OBJECT (priv->player),
        G_CALLBACK (demo_gui_player_errored), gui);
    g_object_unref (priv->player);
  }
  g_object_ref (player);
  priv->player = player;
  g_signal_connect (G_OBJECT (priv->player), "error",
      G_CALLBACK (demo_gui_player_errored), gui);
  g_signal_connect (G_OBJECT (priv->player), "rate-changed",
      G_CALLBACK (demo_gui_rate_changed), gui);
  g_signal_connect (G_OBJECT (priv->player), "playing-started",
      G_CALLBACK (demo_gui_playing_started), gui);
  g_signal_connect (G_OBJECT (priv->player), "playing-paused",
      G_CALLBACK (demo_gui_playing_paused), gui);
  g_signal_connect (G_OBJECT (priv->player), "playing-ended",
      G_CALLBACK (demo_gui_playing_ended), gui);
  priv->is_playing = FALSE;
}

static void
demo_gui_set_playlist_func (DemoGui * gui, GList * uris)
{
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  priv->uris = uris;
}

typedef struct _ActionEntry
{
  GtkAction *action;
  GtkWidget *button;
  const gchar *accel;
  const gchar *name;
  const gchar *label;
  const gchar *tooltip;
  const gchar *stock_id;
  GtkAccelGroup *accel_group;
  GtkActionGroup *action_group;
  GCallback callback;
  gpointer data;
} ActionEntry;

static GValueArray *
build_gvalue_array (guint n_values, ...)
{
  va_list args;
  GValueArray *gva;
  int i;

  va_start (args, n_values);
  gva = g_value_array_new (n_values);

  for (i = 0; i < n_values; i++) {
    GType type = va_arg (args, GType);
    GValue *gval = g_new0 (GValue, 1);
    if (type == G_TYPE_INT) {
      gint value = va_arg (args, gint);
      g_value_set_int (g_value_init (gval, G_TYPE_INT), value);
    } else if (type == G_TYPE_UINT) {
      guint value = va_arg (args, guint);
      g_value_set_uint (g_value_init (gval, G_TYPE_UINT), value);
    } else if (type == G_TYPE_DOUBLE) {
      double value = va_arg (args, double);
      g_value_set_double (g_value_init (gval, G_TYPE_DOUBLE), value);
    } else if (type == G_TYPE_OBJECT) {
      GObject *value = va_arg (args, GObject *);
      g_value_set_object (g_value_init (gval, G_TYPE_OBJECT), value);
    } else {
      g_critical ("build_gvalue_array cannot handle type (%s)",
          g_type_name (type));
      va_end (args);
      return NULL;
    }
    g_value_array_append (gva, gval);
  }
  va_end (args);
  return gva;
}

static void
create_action (ActionEntry * p)
{
  p->action = gtk_action_new (p->name, p->label, p->tooltip, p->stock_id);

  gtk_action_group_add_action_with_accel (p->action_group, p->action, p->accel);
  gtk_action_set_accel_group (p->action, p->accel_group);
  gtk_action_connect_accelerator (p->action);

  p->button = gtk_button_new ();
#if GTK_CHECK_VERSION (2, 16, 0)
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (p->button), p->action);
#else
  gtk_action_connect_proxy (p->action, p->button);
#endif
  gtk_button_set_image (GTK_BUTTON (p->button),
      gtk_action_create_icon (p->action, GTK_ICON_SIZE_BUTTON));
  g_signal_connect (G_OBJECT (p->action), "activate", p->callback, p->data);
}

static void
demo_gui_show_func (DemoGui * gui)
{
  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  GtkWidget *window;
  GtkAccelGroup *accel_group;
  GtkActionGroup *action_group;
  GtkAction *toggle_advanced, *toggle_disabled;
  ActionEntry *slower_lg, *slower_sm, *faster_sm, *faster_lg, *normal,
      *rewind_lg, *rewind_sm, *forward_sm, *forward_lg, *pause, *play,
      *play_pause, *open_file, *playlist_prev, *playlist_next, *quit, *about;
  GtkRequisition pause_size;
  GtkWidget *rate_entry, *rate_label, *toolbox, *stride_ui, *overlap_ui,
      *search_ui, *propbox, *adv_check, *disabled_check, *media_controls,
      *amount_played, *amount_to_play, *seek_range, *seek_bar, *status_bar,
      *file_menu, *file_menu_item, *media_menu_item, *demo_menu,
      *demo_menu_item, *menu_bar, *toplevel_box, *media_menu;
  GError *error = NULL;

  gtk_init (NULL, NULL);
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (window), "destroy", G_CALLBACK (demo_gui_do_quit),
      gui);

  accel_group = gtk_accel_group_new ();
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
  action_group = gtk_action_group_new ("toolbar");

  slower_lg = &(ActionEntry) {
    NULL, NULL,
        "braceleft", "slower-large",
        "2x Slower", "half playback rate",
        GTK_STOCK_GO_DOWN, accel_group, action_group,
        G_CALLBACK (demo_gui_do_change_rate),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_DOUBLE, 0.5)
  };
  create_action (slower_lg);

  slower_sm = &(ActionEntry) {
    NULL, NULL,
        "bracketleft", "slower-small",
        "_Slower", "decrease playback rate",
        GTK_STOCK_GO_DOWN, accel_group, action_group,
        G_CALLBACK (demo_gui_do_change_rate),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_DOUBLE, pow (2,
            -1.0 / 12))
  };
  create_action (slower_sm);

  faster_sm = &(ActionEntry) {
    NULL, NULL,
        "bracketright", "faster-small",
        "_Faster", "increase playback rate",
        GTK_STOCK_GO_UP, accel_group, action_group,
        G_CALLBACK (demo_gui_do_change_rate),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_DOUBLE, pow (2,
            1.0 / 12))
  };
  create_action (faster_sm);

  faster_lg = &(ActionEntry) {
    NULL, NULL,
        "braceright", "faster-large",
        "2X Faster", "double playback rate",
        GTK_STOCK_GO_UP, accel_group, action_group,
        G_CALLBACK (demo_gui_do_change_rate),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_DOUBLE, 2.0)
  };
  create_action (faster_lg);

  normal = &(ActionEntry) {
    NULL, NULL,
        "backslash", "normal",
        "_Normal", "playback normal rate",
        GTK_STOCK_CLEAR, accel_group, action_group,
        G_CALLBACK (demo_gui_do_set_rate),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_DOUBLE, 1.0)
  };
  create_action (normal);

  rewind_lg = &(ActionEntry) {
    NULL, NULL,
        "<ctrl><shift>Left", "seek-rewind-large",
        "Rewind (large)", "seek -30 seconds",
        GTK_STOCK_MEDIA_REWIND, accel_group, action_group,
        G_CALLBACK (demo_gui_do_seek),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_INT, -30)
  };
  create_action (rewind_lg);

  rewind_sm = &(ActionEntry) {
    NULL, NULL,
        "<ctrl>Left", "seek-rewind-small",
        "Rewind", "seek -15 seconds",
        GTK_STOCK_MEDIA_REWIND, accel_group, action_group,
        G_CALLBACK (demo_gui_do_seek),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_INT, -5)
  };
  create_action (rewind_sm);

  forward_sm = &(ActionEntry) {
    NULL, NULL,
        "<ctrl>Right", "seek-forward-small",
        "Forward", "seek +5 seconds",
        GTK_STOCK_MEDIA_FORWARD, accel_group, action_group,
        G_CALLBACK (demo_gui_do_seek),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_INT, 5)
  };
  create_action (forward_sm);

  forward_lg = &(ActionEntry) {
    NULL, NULL,
        "<ctrl><shift>Right", "seek-forward-large",
        "Forward (large)", "seek +30 seconds",
        GTK_STOCK_MEDIA_FORWARD, accel_group, action_group,
        G_CALLBACK (demo_gui_do_seek),
        build_gvalue_array (2, G_TYPE_OBJECT, gui, G_TYPE_INT, 30)
  };
  create_action (forward_lg);

  pause = &(ActionEntry) {
  NULL, NULL,
        "p", "pause",
        "Pause", "Pause playback",
        GTK_STOCK_MEDIA_PAUSE, accel_group, action_group,
        G_CALLBACK (demo_gui_do_pause), gui};
  create_action (pause);

  play = &(ActionEntry) {
  NULL, NULL,
        "<ctrl>p", "play",
        "Play", "Start Playback",
        GTK_STOCK_MEDIA_PLAY, accel_group, action_group,
        G_CALLBACK (demo_gui_do_play), gui};
  create_action (play);
  gtk_widget_size_request (pause->button, &pause_size);
  gtk_widget_set_size_request (play->button, pause_size.width, -1);

  play_pause = &(ActionEntry) {
  NULL, NULL,
        "space", "play-pause",
        "Play/Pause", "Toggle playback",
        NULL, accel_group, action_group,
        G_CALLBACK (demo_gui_do_play_pause), gui};
  create_action (play_pause);

  open_file = &(ActionEntry) {
  NULL, NULL,
        "<ctrl>o", "open-file",
        "Open File", "Open file for playing",
        GTK_STOCK_OPEN, accel_group, action_group,
        G_CALLBACK (demo_gui_do_open_file), gui};
  create_action (open_file);

  playlist_prev = &(ActionEntry) {
  NULL, NULL,
        "less", "playlist-previous",
        "Previous", "Previous in playlist",
        GTK_STOCK_MEDIA_PREVIOUS, accel_group, action_group,
        G_CALLBACK (demo_gui_do_playlist_prev), gui};
  create_action (playlist_prev);

  playlist_next = &(ActionEntry) {
  NULL, NULL,
        "greater", "playlist-next",
        "Next", "Next in playlist",
        GTK_STOCK_MEDIA_NEXT, accel_group, action_group,
        G_CALLBACK (demo_gui_do_playlist_next), gui};
  create_action (playlist_next);

  quit = &(ActionEntry) {
  NULL, NULL,
        "q", "quit",
        "Quit", "Quit demo",
        GTK_STOCK_QUIT, accel_group, action_group,
        G_CALLBACK (demo_gui_do_quit), gui};
  create_action (quit);

  about = &(ActionEntry) {
  NULL, NULL,
        "<ctrl>h", "about",
        "About", "About gst-scaletemo-demo",
        GTK_STOCK_ABOUT, accel_group, action_group,
        G_CALLBACK (demo_gui_do_about_dialog), gui};
  create_action (about);

  rate_entry = gtk_entry_new ();
  rate_label = gtk_label_new ("Rate:");
  gtk_entry_set_max_length (GTK_ENTRY (rate_entry), 5);
  gtk_entry_set_text (GTK_ENTRY (rate_entry), "1.0");
  gtk_entry_set_width_chars (GTK_ENTRY (rate_entry), 5);
  g_signal_connect (G_OBJECT (rate_entry), "activate",
      G_CALLBACK (demo_gui_do_rate_entered), gui);

  toolbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (toolbox), slower_sm->button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toolbox), rate_label, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toolbox), rate_entry, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toolbox), faster_sm->button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toolbox), normal->button, FALSE, FALSE, 2);


  stride_ui =
      gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (60, 1, 1000, 1,
              10, 0)), 0, 0);
  overlap_ui =
      gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (20, 0, 100, 5,
              10, .00001)), 0, 0);
  search_ui =
      gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (14, 0, 1000, 1,
              10, 0)), 0, 0);
  gtk_widget_set_sensitive (stride_ui, FALSE);
  gtk_widget_set_sensitive (overlap_ui, FALSE);
  gtk_widget_set_sensitive (search_ui, FALSE);
  g_signal_connect (G_OBJECT (stride_ui), "output",
      G_CALLBACK (demo_gui_request_set_stride), gui);
  g_signal_connect (G_OBJECT (overlap_ui), "output",
      G_CALLBACK (demo_gui_request_set_overlap), gui);
  g_signal_connect (G_OBJECT (search_ui), "output",
      G_CALLBACK (demo_gui_request_set_search), gui);
  g_signal_connect (G_OBJECT (priv->player), "notify::stride",
      G_CALLBACK (demo_gui_stride_changed), build_gvalue_array (2,
          G_TYPE_OBJECT, gui, G_TYPE_OBJECT, stride_ui));
  g_signal_connect (G_OBJECT (priv->player), "notify::overlap",
      G_CALLBACK (demo_gui_overlap_changed), build_gvalue_array (2,
          G_TYPE_OBJECT, gui, G_TYPE_OBJECT, overlap_ui));
  g_signal_connect (G_OBJECT (priv->player), "notify::search",
      G_CALLBACK (demo_gui_search_changed), build_gvalue_array (2,
          G_TYPE_OBJECT, gui, G_TYPE_OBJECT, search_ui));
  propbox = gtk_hbox_new (FALSE, 0);
  adv_check = gtk_check_button_new ();
  gtk_box_pack_start (GTK_BOX (propbox), gtk_label_new ("stride:"), FALSE,
      FALSE, 2);
  gtk_box_pack_start (GTK_BOX (propbox), stride_ui, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (propbox), gtk_label_new ("overlap:"), FALSE,
      FALSE, 2);
  gtk_box_pack_start (GTK_BOX (propbox), overlap_ui, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (propbox), gtk_label_new ("search:"), FALSE,
      FALSE, 2);
  gtk_box_pack_start (GTK_BOX (propbox), search_ui, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (propbox), adv_check, FALSE, FALSE, 2);

  toggle_advanced =
      GTK_ACTION (gtk_toggle_action_new ("advanced", "Enable Parameters",
          "Toggle advanced controls", 0));
  gtk_action_group_add_action_with_accel (action_group, toggle_advanced,
      "<ctrl>a");
  gtk_action_set_accel_group (toggle_advanced, accel_group);
  gtk_action_connect_accelerator (toggle_advanced);
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (toggle_advanced), FALSE);
#if GTK_CHECK_VERSION (2, 16, 0)
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (adv_check),
      toggle_advanced);
#else
  gtk_action_connect_proxy (toggle_advanced, adv_check);
#endif
  g_signal_connect (G_OBJECT (toggle_advanced), "activate",
      G_CALLBACK (demo_gui_do_toggle_advanced), build_gvalue_array (4,
          G_TYPE_OBJECT, gui, G_TYPE_OBJECT, stride_ui, G_TYPE_OBJECT,
          overlap_ui, G_TYPE_OBJECT, search_ui));

  toggle_disabled =
      GTK_ACTION (gtk_toggle_action_new ("disabled", "Disable Scaletempo",
          "Toggle disabling scaletempo", 0));
  gtk_action_group_add_action_with_accel (action_group, toggle_disabled,
      "<ctrl>d");
  gtk_action_set_accel_group (toggle_disabled, accel_group);
  gtk_action_connect_accelerator (toggle_disabled);
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (toggle_disabled), FALSE);
  disabled_check = gtk_check_button_new ();
#if GTK_CHECK_VERSION (2, 16, 0)
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (disabled_check),
      toggle_disabled);
#else
  gtk_action_connect_proxy (toggle_disabled, disabled_check);
#endif
  g_signal_connect (G_OBJECT (toggle_disabled), "activate",
      G_CALLBACK (demo_gui_do_toggle_disabled), build_gvalue_array (3,
          G_TYPE_OBJECT, gui, G_TYPE_OBJECT, toggle_advanced, G_TYPE_OBJECT,
          propbox));
  gtk_box_pack_start (GTK_BOX (toolbox), disabled_check, FALSE, FALSE, 2);


  media_controls = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (media_controls), playlist_prev->button, FALSE,
      FALSE, 2);
  gtk_box_pack_start (GTK_BOX (media_controls), rewind_sm->button, FALSE, FALSE,
      2);
  gtk_box_pack_start (GTK_BOX (media_controls), play->button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (media_controls), pause->button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (media_controls), forward_sm->button, FALSE,
      FALSE, 2);
  gtk_box_pack_start (GTK_BOX (media_controls), playlist_next->button, FALSE,
      FALSE, 2);

  amount_played = gtk_label_new ("?:??:??");
  amount_to_play = gtk_label_new ("-?:??:??");
  gtk_label_set_width_chars (GTK_LABEL (amount_played), 8);
  gtk_label_set_width_chars (GTK_LABEL (amount_to_play), 8);
  gtk_misc_set_alignment (GTK_MISC (amount_played), 1, 1);
  gtk_misc_set_alignment (GTK_MISC (amount_to_play), 0, 1);
  seek_range =
      gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 5.0,
              30.0, 0.00)));
#if !GTK_CHECK_VERSION (3, 0, 0)
  gtk_range_set_update_policy (GTK_RANGE (seek_range),
      GTK_UPDATE_DISCONTINUOUS);
#endif
  seek_bar = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (seek_bar), amount_played, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (seek_bar), seek_range, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (seek_bar), amount_to_play, FALSE, FALSE, 2);
  g_signal_connect (G_OBJECT (seek_range), "format-value",
      G_CALLBACK (demo_gui_seek_bar_format), gui);
  g_signal_connect (G_OBJECT (seek_range), "change-value",
      G_CALLBACK (demo_gui_seek_bar_change), gui);

  status_bar = gtk_statusbar_new ();

  /* Menubar */
  file_menu = gtk_menu_new ();
  gtk_menu_set_accel_group (GTK_MENU (file_menu), accel_group);
  gtk_menu_shell_append (GTK_MENU_SHELL (file_menu),
      gtk_action_create_menu_item (open_file->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (file_menu),
      gtk_action_create_menu_item (about->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (file_menu),
      gtk_action_create_menu_item (quit->action));
  file_menu_item = gtk_menu_item_new_with_mnemonic ("_File");
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (file_menu_item), file_menu);

  media_menu = gtk_menu_new ();
  gtk_menu_set_accel_group (GTK_MENU (media_menu), accel_group);
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (rewind_lg->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (rewind_sm->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (forward_sm->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (forward_lg->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (play->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (pause->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (play_pause->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (playlist_prev->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (media_menu),
      gtk_action_create_menu_item (playlist_next->action));
  media_menu_item = gtk_menu_item_new_with_mnemonic ("_Media");
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (media_menu_item), media_menu);

  demo_menu = gtk_menu_new ();
  gtk_menu_set_accel_group (GTK_MENU (demo_menu), accel_group);
  gtk_menu_shell_append (GTK_MENU_SHELL (demo_menu),
      gtk_action_create_menu_item (faster_lg->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (demo_menu),
      gtk_action_create_menu_item (faster_sm->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (demo_menu),
      gtk_action_create_menu_item (slower_sm->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (demo_menu),
      gtk_action_create_menu_item (slower_lg->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (demo_menu),
      gtk_action_create_menu_item (normal->action));
  gtk_menu_shell_append (GTK_MENU_SHELL (demo_menu),
      gtk_action_create_menu_item (toggle_disabled));
  gtk_menu_shell_append (GTK_MENU_SHELL (demo_menu),
      gtk_action_create_menu_item (toggle_advanced));
  demo_menu_item = gtk_menu_item_new_with_mnemonic ("_Scaletempo");
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (demo_menu_item), demo_menu);

  menu_bar = gtk_menu_bar_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), file_menu_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), media_menu_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), demo_menu_item);

  /* Toplevel Window */
  gtk_window_set_title (GTK_WINDOW (window), "Scaletempo Demo");
  toplevel_box = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (toplevel_box), 3);
  gtk_container_add (GTK_CONTAINER (window), toplevel_box);
  gtk_box_pack_start (GTK_BOX (toplevel_box), menu_bar, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toplevel_box), media_controls, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toplevel_box), toolbox, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toplevel_box), propbox, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toplevel_box), seek_bar, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (toplevel_box), status_bar, FALSE, FALSE, 2);

  priv->window = window;
  priv->rate_entry = GTK_ENTRY (rate_entry);
  priv->status_bar = GTK_STATUSBAR (status_bar);
  priv->seek_range = GTK_RANGE (seek_range);
  priv->amount_played = GTK_LABEL (amount_played);
  priv->amount_to_play = GTK_LABEL (amount_to_play);
  priv->play_action = GTK_ACTION (play->action);
  priv->pause_action = GTK_ACTION (pause->action);
  priv->open_file = GTK_ACTION (open_file->action);
  priv->playlist_next = GTK_ACTION (playlist_next->action);

  gtk_action_set_sensitive (priv->pause_action, FALSE);
  gtk_action_set_visible (priv->pause_action, FALSE);

  gtk_widget_show_all (window);
  gtk_widget_grab_focus (seek_range);
  gtk_action_activate (priv->playlist_next);
  status_bar_printf (GTK_STATUSBAR (status_bar), 5,
      "Welcome to the Scaletempo demo.");

  if (!g_thread_create ((GThreadFunc) gtk_main, NULL, FALSE, &error)) {
    g_signal_emit (gui, demo_gui_signals[SIGNAL_ERROR], 0, error->message);
  }
}


/* Method wrappers */
void
demo_gui_set_player (DemoGui * gui, DemoPlayer * player)
{
  g_return_if_fail (DEMO_IS_GUI (gui));
  g_return_if_fail (DEMO_IS_PLAYER (player));

  DEMO_GUI_GET_CLASS (gui)->set_player (gui, player);
}

void
demo_gui_set_playlist (DemoGui * gui, GList * uris)
{
  g_return_if_fail (DEMO_IS_GUI (gui));

  DEMO_GUI_GET_CLASS (gui)->set_playlist (gui, uris);
}

void
demo_gui_show (DemoGui * gui)
{
  g_return_if_fail (DEMO_IS_GUI (gui));

  DEMO_GUI_GET_CLASS (gui)->show (gui);
}



/* GObject overrides */
static void
demo_gui_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  //DemoGui *gui = DEMO_GUI (object);
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
demo_gui_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  //DemoGui *gui = DEMO_GUI (object);
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


/* GTypeInfo functions */
static void
demo_gui_init (GTypeInstance * instance, gpointer klass)
{
  DemoGui *gui = (DemoGui *) instance;

  DemoGuiPrivate *priv = DEMO_GUI_GET_PRIVATE (gui);
  priv->player = NULL;
  priv->uris = NULL;
  priv->now_playing = NULL;
  priv->is_playing = FALSE;
  priv->window = NULL;
  priv->rate_entry = NULL;
  priv->position_updater_id = 0;
  priv->seek_range = NULL;
  priv->amount_played = NULL;
  priv->amount_to_play = NULL;
}

static void
demo_gui_class_init (gpointer klass, gpointer class_data)
{
  DemoGuiClass *gui_class = (DemoGuiClass *) klass;
  GObjectClass *as_object_class = G_OBJECT_CLASS (klass);
  GType type;

  g_type_class_add_private (klass, sizeof (DemoGuiPrivate));

  /* DemiPlayer */
  gui_class->set_player = demo_gui_set_player_func;
  gui_class->set_playlist = demo_gui_set_playlist_func;
  gui_class->show = demo_gui_show_func;

  /* GObject */
  as_object_class->get_property = demo_gui_get_property;
  as_object_class->set_property = demo_gui_set_property;

  /* Properties */

  /* Signals */
  type = G_TYPE_FROM_CLASS (klass);
  demo_gui_signals[SIGNAL_ERROR] = g_signal_new ("error", type,
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING);

  demo_gui_signals[SIGNAL_QUITING] = g_signal_new ("quiting", type,
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}

GType
demo_gui_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0)) {
    static const GTypeInfo info = {
      sizeof /* Class */ (DemoGuiClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) demo_gui_class_init,
      (GClassFinalizeFunc) NULL,
      (gconstpointer) NULL,     /* class_data */
      sizeof /* Instance */ (DemoGui),
      /* n_preallocs */ 0,
      (GInstanceInitFunc) demo_gui_init,
      (const GTypeValueTable *) NULL
    };
    type = g_type_register_static (G_TYPE_OBJECT, "DemoGui", &info, 0);
  }
  return type;
}
