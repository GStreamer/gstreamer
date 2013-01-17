/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <ges/ges.h>

/* Application Data ********************************************************/

/**
 * Contains most of the application data so that signal handlers
 * and other callbacks have easy access.
 */

typedef struct App
{
  /* back-end objects */
  GESTimeline *timeline;
  GESTimelinePipeline *pipeline;
  GESTimelineLayer *layer;
  GESTrack *audio_track;
  GESTrack *video_track;
  guint audio_tracks;
  guint video_tracks;

  /* application state */
  gchar *pending_uri;
  int n_objects;

  int n_selected;
  GList *selected_objects;
  GType selected_type;
  gboolean first_selected;
  gboolean last_selected;

  gboolean ignore_input;
  GstState state;

  GtkListStore *model;
  GtkTreeSelection *selection;

  /* widgets */
  GtkWidget *main_window;
  GtkWidget *add_effect_dlg;
  GtkWidget *properties;
  GtkWidget *filesource_properties;
  GtkWidget *text_properties;
  GtkWidget *generic_duration;
  GtkWidget *background_properties;
  GtkWidget *audio_effect_entry;
  GtkWidget *video_effect_entry;

  GtkHScale *duration;
  GtkHScale *in_point;
  GtkHScale *volume;

  GtkAction *add_file;
  GtkAction *add_effect;
  GtkAction *add_test;
  GtkAction *add_title;
  GtkAction *add_transition;
  GtkAction *delete;
  GtkAction *play;
  GtkAction *stop;
  GtkAction *move_up;
  GtkAction *move_down;
  GtkToggleAction *audio_track_action;
  GtkToggleAction *video_track_action;

  GtkComboBox *halign;
  GtkComboBox *valign;
  GtkComboBox *background_type;

  GtkEntry *text;
  GtkEntry *seconds;

  GtkSpinButton *frequency;
} App;

static int n_instances = 0;

/* Prototypes for auto-connected signal handlers ***************************/

/**
 * These are declared non-static for signal auto-connection
 */

gboolean window_delete_event_cb (GtkObject * window, GdkEvent * event,
    App * app);
void new_activate_cb (GtkMenuItem * item, App * app);
void open_activate_cb (GtkMenuItem * item, App * app);
void save_as_activate_cb (GtkMenuItem * item, App * app);
void launch_pitivi_project_activate_cb (GtkMenuItem * item, App * app);
void quit_item_activate_cb (GtkMenuItem * item, App * app);
void delete_activate_cb (GtkAction * item, App * app);
void play_activate_cb (GtkAction * item, App * app);
void stop_activate_cb (GtkAction * item, App * app);
void move_up_activate_cb (GtkAction * item, App * app);
void move_down_activate_cb (GtkAction * item, App * app);
void add_effect_activate_cb (GtkAction * item, App * app);
void add_file_activate_cb (GtkAction * item, App * app);
void add_text_activate_cb (GtkAction * item, App * app);
void add_test_activate_cb (GtkAction * item, App * app);
void audio_track_activate_cb (GtkToggleAction * item, App * app);
void video_track_activate_cb (GtkToggleAction * item, App * app);
void add_transition_activate_cb (GtkAction * item, App * app);
void app_selection_changed_cb (GtkTreeSelection * selection, App * app);
void halign_changed_cb (GtkComboBox * widget, App * app);
void valign_changed_cb (GtkComboBox * widget, App * app);
void background_type_changed_cb (GtkComboBox * widget, App * app);
void frequency_value_changed_cb (GtkSpinButton * widget, App * app);
void on_apply_effect_cb (GtkButton * button, App * app);
void on_cancel_add_effect_cb (GtkButton * button, App * app);
gboolean add_effect_dlg_delete_event_cb (GtkWidget * widget, GdkEvent * event,
    gpointer * app);

gboolean
duration_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);

gboolean
in_point_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);

gboolean
volume_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);

/* UI state functions *******************************************************/

/**
 * Update properties of UI elements that depend on more than one thing.
 */

static void
update_effect_sensitivity (App * app)
{
  GList *i;
  gboolean ok = TRUE;

  /* effects will work for multiple FileSource */
  for (i = app->selected_objects; i; i = i->next) {
    if (!GES_IS_URI_CLIP (i->data)) {
      ok = FALSE;
      break;
    }
  }

  gtk_action_set_sensitive (app->add_effect,
      ok && (app->n_selected > 0) && (app->state != GST_STATE_PLAYING)
      && (app->state != GST_STATE_PAUSED));
}

static void
update_delete_sensitivity (App * app)
{
  /* delete will work for multiple items */
  gtk_action_set_sensitive (app->delete,
      (app->n_selected > 0) && (app->state != GST_STATE_PLAYING)
      && (app->state != GST_STATE_PAUSED));
}

static void
update_add_transition_sensitivity (App * app)
{
  gtk_action_set_sensitive (app->add_transition,
      (app->state != GST_STATE_PLAYING) && (app->state != GST_STATE_PAUSED));
}

static void
update_move_up_down_sensitivity (App * app)
{
  gboolean can_move;

  can_move = (app->n_selected == 1) &&
      (app->state != GST_STATE_PLAYING) && (app->state != GST_STATE_PAUSED);

  gtk_action_set_sensitive (app->move_up, can_move && (!app->first_selected));
  gtk_action_set_sensitive (app->move_down, can_move && (!app->last_selected));
}

static void
update_play_sensitivity (App * app)
{
  gboolean valid;

  g_object_get (app->layer, "valid", &valid, NULL);

  gtk_action_set_sensitive (app->play, (app->n_objects && valid));
}

/* Backend callbacks ********************************************************/

static void
test_source_notify_volume_changed_cb (GESClip * object, GParamSpec *
    unused G_GNUC_UNUSED, App * app)
{
  gdouble volume;

  g_object_get (G_OBJECT (object), "volume", &volume, NULL);

  gtk_range_set_value (GTK_RANGE (app->volume), volume);
}

static void
layer_notify_valid_changed_cb (GObject * object, GParamSpec * unused
    G_GNUC_UNUSED, App * app)
{
  update_play_sensitivity (app);
}

static gboolean
find_row_for_object (GtkListStore * model, GtkTreeIter * ret, GESClip * object)
{
  gtk_tree_model_get_iter_first ((GtkTreeModel *) model, ret);

  while (gtk_list_store_iter_is_valid (model, ret)) {
    GESClip *obj;
    gtk_tree_model_get ((GtkTreeModel *) model, ret, 2, &obj, -1);
    if (obj == object) {
      g_object_unref (obj);
      return TRUE;
    }
    g_object_unref (obj);
    gtk_tree_model_iter_next ((GtkTreeModel *) model, ret);
  }
  return FALSE;
}

/* this callback is registered for every timeline object, and updates the
 * corresponding duration cell in the model */
static void
clip_notify_duration_cb (GESClip * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  GtkTreeIter iter;
  guint64 duration = 0;

  g_object_get (object, "duration", &duration, NULL);
  find_row_for_object (app->model, &iter, object);
  gtk_list_store_set (app->model, &iter, 1, duration, -1);
}

/* these guys are only connected to filesources that are the target of the
 * current selection */

static void
filesource_notify_duration_cb (GESClip * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  guint64 duration, max_inpoint;
  duration = GES_TIMELINE_ELEMENT_DURATION (object);
  max_inpoint = GES_TIMELINE_ELEMENT_MAX_DURATION (object) - duration;

  gtk_range_set_value (GTK_RANGE (app->duration), duration);
  gtk_range_set_fill_level (GTK_RANGE (app->in_point), max_inpoint);

  if (max_inpoint < GES_TIMELINE_ELEMENT_INPOINT (object))
    g_object_set (object, "in-point", max_inpoint, NULL);

}

static void
filesource_notify_max_duration_cb (GESClip * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  gtk_range_set_range (GTK_RANGE (app->duration), 0, (gdouble)
      GES_TIMELINE_ELEMENT_MAX_DURATION (object));
  gtk_range_set_range (GTK_RANGE (app->in_point), 0, (gdouble)
      GES_TIMELINE_ELEMENT_MAX_DURATION (object));
}

static void
filesource_notify_in_point_cb (GESClip * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  gtk_range_set_value (GTK_RANGE (app->in_point),
      GES_TIMELINE_ELEMENT_INPOINT (object));
}

static void
app_update_first_last_selected (App * app)
{
  GtkTreePath *path;

  /* keep track of whether the first or last items are selected */
  path = gtk_tree_path_new_from_indices (0, -1);
  app->first_selected =
      gtk_tree_selection_path_is_selected (app->selection, path);
  gtk_tree_path_free (path);

  path = gtk_tree_path_new_from_indices (app->n_objects - 1, -1);
  app->last_selected =
      gtk_tree_selection_path_is_selected (app->selection, path);
  gtk_tree_path_free (path);
}

static void
object_count_changed (App * app)
{
  app_update_first_last_selected (app);
  update_move_up_down_sensitivity (app);
  update_play_sensitivity (app);
}

static void
title_source_text_changed_cb (GESClip * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  GtkTreeIter iter;
  gchar *text;

  g_object_get (object, "text", &text, NULL);
  if (text) {
    find_row_for_object (app->model, &iter, object);
    gtk_list_store_set (app->model, &iter, 0, text, -1);
  }
}

static void
layer_object_added_cb (GESTimelineLayer * layer, GESClip * object, App * app)
{
  GtkTreeIter iter;
  gchar *description;

  GST_INFO ("layer object added cb %p %p %p", layer, object, app);

  gtk_list_store_append (app->model, &iter);

  if (GES_IS_URI_CLIP (object)) {
    g_object_get (G_OBJECT (object), "uri", &description, NULL);
    gtk_list_store_set (app->model, &iter, 0, description, 2, object, -1);
  }

  else if (GES_IS_TIMELINE_TITLE_SOURCE (object)) {
    gtk_list_store_set (app->model, &iter, 2, object, -1);
    g_signal_connect (G_OBJECT (object), "notify::text",
        G_CALLBACK (title_source_text_changed_cb), app);
    title_source_text_changed_cb (object, NULL, app);
  }

  else if (GES_IS_TIMELINE_TEST_SOURCE (object)) {
    gtk_list_store_set (app->model, &iter, 2, object, 0, "Test Source", -1);
  }

  else if (GES_IS_TRANSITION_CLIP (object)) {
    gtk_list_store_set (app->model, &iter, 2, object, 0, "Transition", -1);
  }

  g_signal_connect (G_OBJECT (object), "notify::duration",
      G_CALLBACK (clip_notify_duration_cb), app);
  clip_notify_duration_cb (object, NULL, app);

  app->n_objects++;
  object_count_changed (app);
}

static void
layer_object_removed_cb (GESTimelineLayer * layer, GESClip * object, App * app)
{
  GtkTreeIter iter;

  GST_INFO ("layer object removed cb %p %p %p", layer, object, app);

  if (!find_row_for_object (GTK_LIST_STORE (app->model), &iter, object)) {
    g_print ("object deleted but we don't own it");
    return;
  }
  app->n_objects--;
  object_count_changed (app);

  gtk_list_store_remove (app->model, &iter);
}

static void
layer_object_moved_cb (GESClip * layer, GESClip * object,
    gint old, gint new, App * app)
{
  GtkTreeIter a, b;
  GtkTreePath *path;

  /* we can take the old position as given, but the new position might have to
   * be adjusted. */
  new = new < 0 ? (app->n_objects - 1) : new;

  path = gtk_tree_path_new_from_indices (old, -1);
  gtk_tree_model_get_iter (GTK_TREE_MODEL (app->model), &a, path);
  gtk_tree_path_free (path);

  path = gtk_tree_path_new_from_indices (new, -1);
  gtk_tree_model_get_iter (GTK_TREE_MODEL (app->model), &b, path);
  gtk_tree_path_free (path);

  gtk_list_store_swap (app->model, &a, &b);
  app_selection_changed_cb (app->selection, app);
  update_move_up_down_sensitivity (app);
}

static void
pipeline_state_changed_cb (App * app)
{
  gboolean playing_or_paused;

  if (app->state == GST_STATE_PLAYING)
    gtk_action_set_stock_id (app->play, GTK_STOCK_MEDIA_PAUSE);
  else
    gtk_action_set_stock_id (app->play, GTK_STOCK_MEDIA_PLAY);

  update_delete_sensitivity (app);
  update_add_transition_sensitivity (app);
  update_move_up_down_sensitivity (app);

  playing_or_paused = (app->state == GST_STATE_PLAYING) ||
      (app->state == GST_STATE_PAUSED);

  gtk_action_set_sensitive (app->add_file, !playing_or_paused);
  gtk_action_set_sensitive (app->add_title, !playing_or_paused);
  gtk_action_set_sensitive (app->add_test, !playing_or_paused);
  gtk_action_set_sensitive ((GtkAction *) app->audio_track_action,
      !playing_or_paused);
  gtk_action_set_sensitive ((GtkAction *) app->video_track_action,
      !playing_or_paused);
  gtk_widget_set_sensitive (app->properties, !playing_or_paused);
}

static void
project_bus_message_cb (GstBus * bus, GstMessage * message,
    GMainLoop * mainloop)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_printerr ("ERROR\n");
      g_main_loop_quit (mainloop);
      break;
    case GST_MESSAGE_EOS:
      g_printerr ("Done\n");
      g_main_loop_quit (mainloop);
      break;
    default:
      break;
  }
}

static void
bus_message_cb (GstBus * bus, GstMessage * message, App * app)
{
  const GstStructure *s;
  s = gst_message_get_structure (message);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("ERROR\n");
      break;
    case GST_MESSAGE_EOS:
      gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_READY);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (s && GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (app->pipeline)) {
        GstState old, new, pending;
        gst_message_parse_state_changed (message, &old, &new, &pending);
        app->state = new;
        pipeline_state_changed_cb (app);
      }
      break;
    default:
      break;
  }
}

/* Static UI Callbacks ******************************************************/

static gboolean
check_time (const gchar * time)
{
  static GRegex *re = NULL;

  if (!re) {
    if (NULL == (re =
            g_regex_new ("^[0-9][0-9]:[0-5][0-9]:[0-5][0-9](\\.[0-9]+)?$",
                G_REGEX_EXTENDED, 0, NULL)))
      return FALSE;
  }

  if (g_regex_match (re, time, 0, NULL))
    return TRUE;
  return FALSE;
}

static guint64
str_to_time (const gchar * str)
{
  guint64 ret;
  guint64 h, m;
  gdouble s;
  gchar buf[15];

  buf[0] = str[0];
  buf[1] = str[1];
  buf[2] = '\0';

  h = strtoull (buf, NULL, 10);

  buf[0] = str[3];
  buf[1] = str[4];
  buf[2] = '\0';

  m = strtoull (buf, NULL, 10);

  strncpy (buf, &str[6], sizeof (buf));
  s = strtod (buf, NULL);

  ret = (h * 3600 * GST_SECOND) +
      (m * 60 * GST_SECOND) + ((guint64) (s * GST_SECOND));

  return ret;
}

static void
text_notify_text_changed_cb (GtkEntry * widget, GParamSpec * unused, App * app)
{
  GList *tmp;
  const gchar *text;

  if (app->ignore_input)
    return;

  text = gtk_entry_get_text (widget);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "text", text, NULL);
  }
}

static void
seconds_notify_text_changed_cb (GtkEntry * widget, GParamSpec * unused,
    App * app)
{
  GList *tmp;
  const gchar *text;

  if (app->ignore_input)
    return;

  text = gtk_entry_get_text (app->seconds);

  if (!check_time (text)) {
    gtk_entry_set_icon_from_stock (app->seconds,
        GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_DIALOG_WARNING);
  } else {
    gtk_entry_set_icon_from_stock (app->seconds,
        GTK_ENTRY_ICON_SECONDARY, NULL);
    for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
      g_object_set (GES_CLIP (tmp->data), "duration",
          (guint64) str_to_time (text), NULL);
    }
  }
}

static void
duration_cell_func (GtkTreeViewColumn * column, GtkCellRenderer * renderer,
    GtkTreeModel * model, GtkTreeIter * iter, gpointer user)
{
  gchar buf[30];
  guint64 duration;

  gtk_tree_model_get (model, iter, 1, &duration, -1);
  g_snprintf (buf, sizeof (buf), "%u:%02u:%02u.%09u", GST_TIME_ARGS (duration));
  g_object_set (renderer, "text", &buf, NULL);
}

/* UI Initialization ********************************************************/

static void
connect_to_filesource (GESClip * object, App * app)
{
  g_signal_connect (G_OBJECT (object), "notify::max-duration",
      G_CALLBACK (filesource_notify_max_duration_cb), app);
  filesource_notify_max_duration_cb (object, NULL, app);

  g_signal_connect (G_OBJECT (object), "notify::duration",
      G_CALLBACK (filesource_notify_duration_cb), app);
  filesource_notify_duration_cb (object, NULL, app);

  g_signal_connect (G_OBJECT (object), "notify::in-point",
      G_CALLBACK (filesource_notify_in_point_cb), app);
  filesource_notify_in_point_cb (object, NULL, app);
}

static void
disconnect_from_filesource (GESClip * object, App * app)
{
  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      filesource_notify_duration_cb, app);

  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      filesource_notify_max_duration_cb, app);
}

static void
connect_to_title_source (GESClip * object, App * app)
{
  GESTimelineTitleSource *obj;
  obj = GES_TIMELINE_TITLE_SOURCE (object);
  gtk_combo_box_set_active (app->halign,
      ges_timeline_title_source_get_halignment (obj));
  gtk_combo_box_set_active (app->valign,
      ges_timeline_title_source_get_valignment (obj));
  gtk_entry_set_text (app->text, ges_timeline_title_source_get_text (obj));
}

static void
disconnect_from_title_source (GESClip * object, App * app)
{
}

static void
connect_to_test_source (GESClip * object, App * app)
{
  GObjectClass *klass;
  GParamSpecDouble *pspec;

  GESTimelineTestSource *obj;
  obj = GES_TIMELINE_TEST_SOURCE (object);
  gtk_combo_box_set_active (app->background_type,
      ges_timeline_test_source_get_vpattern (obj));

  g_signal_connect (G_OBJECT (object), "notify::volume",
      G_CALLBACK (test_source_notify_volume_changed_cb), app);
  test_source_notify_volume_changed_cb (object, NULL, app);

  klass = G_OBJECT_GET_CLASS (G_OBJECT (object));

  pspec = G_PARAM_SPEC_DOUBLE (g_object_class_find_property (klass, "volume"));
  gtk_range_set_range (GTK_RANGE (app->volume), pspec->minimum, pspec->maximum);

  pspec = G_PARAM_SPEC_DOUBLE (g_object_class_find_property (klass, "freq"));
  gtk_spin_button_set_range (app->frequency, pspec->minimum, pspec->maximum);
  gtk_spin_button_set_value (app->frequency,
      ges_timeline_test_source_get_frequency (GES_TIMELINE_TEST_SOURCE
          (object)));
}

static void
disconnect_from_test_source (GESClip * object, App * app)
{
  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      test_source_notify_volume_changed_cb, app);
}

static void
connect_to_object (GESClip * object, App * app)
{
  gchar buf[30];
  guint64 duration;

  app->ignore_input = TRUE;

  duration = GES_TIMELINE_ELEMENT_DURATION (object);
  g_snprintf (buf, sizeof (buf), "%02u:%02u:%02u.%09u",
      GST_TIME_ARGS (duration));
  gtk_entry_set_text (app->seconds, buf);

  if (GES_IS_URI_CLIP (object)) {
    connect_to_filesource (object, app);
  } else if (GES_IS_TIMELINE_TITLE_SOURCE (object)) {
    connect_to_title_source (object, app);
  } else if (GES_IS_TIMELINE_TEST_SOURCE (object)) {
    connect_to_test_source (object, app);
  }

  app->ignore_input = FALSE;
}

static void
disconnect_from_object (GESClip * object, App * app)
{
  if (GES_IS_URI_CLIP (object)) {
    disconnect_from_filesource (object, app);
  } else if (GES_IS_TIMELINE_TITLE_SOURCE (object)) {
    disconnect_from_title_source (object, app);
  } else if (GES_IS_TIMELINE_TEST_SOURCE (object)) {
    disconnect_from_test_source (object, app);
  }
}

static GtkListStore *
get_video_patterns (void)
{
  GEnumClass *enum_class;
  GESTimelineTestSource *tr;
  GESTimelineTestSourceClass *klass;
  GParamSpec *pspec;
  GEnumValue *v;
  GtkListStore *m;
  GtkTreeIter i;

  m = gtk_list_store_new (1, G_TYPE_STRING);

  tr = ges_timeline_test_source_new ();
  klass = GES_TIMELINE_TEST_SOURCE_GET_CLASS (tr);

  pspec = g_object_class_find_property (G_OBJECT_CLASS (klass), "vpattern");

  enum_class = G_ENUM_CLASS (g_type_class_ref (pspec->value_type));

  for (v = enum_class->values; v->value_nick != NULL; v++) {
    gtk_list_store_append (m, &i);
    gtk_list_store_set (m, &i, 0, v->value_name, -1);
  }

  g_type_class_unref (enum_class);
  g_object_unref (tr);

  return m;
}

#define GET_WIDGET(dest,name,type) {\
  if (!(dest =\
    type(gtk_builder_get_object(builder, name))))\
        goto fail;\
}


static void
layer_added_cb (GESTimeline * timeline, GESTimelineLayer * layer, App * app)
{
  if (!GES_IS_SIMPLE_TIMELINE_LAYER (layer)) {
    GST_ERROR ("This timeline contains a layer type other than "
        "GESSimpleTimelineLayer. Timeline editing disabled");
    return;
  }

  if (!(app->layer)) {
    app->layer = layer;
  }

  if (layer != app->layer) {
    GST_ERROR ("This demo doesn't support editing timelines with multiple"
        " layers");
    return;
  }

  g_signal_connect (app->layer, "object-added",
      G_CALLBACK (layer_object_added_cb), app);
  g_signal_connect (app->layer, "object-removed",
      G_CALLBACK (layer_object_removed_cb), app);
  g_signal_connect (app->layer, "object-moved",
      G_CALLBACK (layer_object_moved_cb), app);
  g_signal_connect (app->layer, "notify::valid",
      G_CALLBACK (layer_notify_valid_changed_cb), app);
}

static void
update_track_actions (App * app)
{
  g_signal_handlers_disconnect_by_func (app->audio_track_action,
      audio_track_activate_cb, app);
  g_signal_handlers_disconnect_by_func (app->video_track_action,
      video_track_activate_cb, app);
  gtk_toggle_action_set_active (app->audio_track_action, app->audio_tracks);
  gtk_toggle_action_set_active (app->video_track_action, app->video_tracks);
  gtk_action_set_sensitive ((GtkAction *) app->audio_track_action,
      app->audio_tracks <= 1);
  gtk_action_set_sensitive ((GtkAction *) app->video_track_action,
      app->video_tracks <= 1);
  g_signal_connect (G_OBJECT (app->audio_track_action), "activate",
      G_CALLBACK (audio_track_activate_cb), app);
  g_signal_connect (G_OBJECT (app->video_track_action), "activate",
      G_CALLBACK (video_track_activate_cb), app);
}

static void
track_added_cb (GESTimeline * timeline, GESTrack * track, App * app)
{
  if (track->type == GES_TRACK_TYPE_AUDIO) {
    app->audio_tracks++;
    if (!app->audio_track)
      app->audio_track = track;
  }
  if (track->type == GES_TRACK_TYPE_VIDEO) {
    app->video_tracks++;
    if (!app->video_track)
      app->video_track = track;
  }


  update_track_actions (app);
}

static void
track_removed_cb (GESTimeline * timeline, GESTrack * track, App * app)
{
  if (track->type == GES_TRACK_TYPE_AUDIO)
    app->audio_tracks--;
  if (track->type == GES_TRACK_TYPE_VIDEO)
    app->video_tracks--;

  update_track_actions (app);
}

static gboolean
create_ui (App * app)
{
  GtkBuilder *builder;
  GtkTreeView *timeline;
  GtkTreeViewColumn *duration_col;
  GtkCellRenderer *duration_renderer;
  GtkCellRenderer *background_type_renderer;
  GtkListStore *backgrounds;
  GstBus *bus;

  /* construct widget tree */

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);

  /* get a bunch of widgets from the XML tree */

  GET_WIDGET (timeline, "timeline_treeview", GTK_TREE_VIEW);
  GET_WIDGET (app->properties, "properties", GTK_WIDGET);
  GET_WIDGET (app->filesource_properties, "filesource_properties", GTK_WIDGET);
  GET_WIDGET (app->text_properties, "text_properties", GTK_WIDGET);
  GET_WIDGET (app->main_window, "window", GTK_WIDGET);
  GET_WIDGET (app->add_effect_dlg, "add_effect_dlg", GTK_WIDGET);
  GET_WIDGET (app->audio_effect_entry, "entry1", GTK_WIDGET);
  GET_WIDGET (app->video_effect_entry, "entry2", GTK_WIDGET);
  GET_WIDGET (app->duration, "duration_scale", GTK_HSCALE);
  GET_WIDGET (app->in_point, "in_point_scale", GTK_HSCALE);
  GET_WIDGET (app->halign, "halign", GTK_COMBO_BOX);
  GET_WIDGET (app->valign, "valign", GTK_COMBO_BOX);
  GET_WIDGET (app->text, "text", GTK_ENTRY);
  GET_WIDGET (duration_col, "duration_column", GTK_TREE_VIEW_COLUMN);
  GET_WIDGET (duration_renderer, "duration_renderer", GTK_CELL_RENDERER);
  GET_WIDGET (app->add_file, "add_file", GTK_ACTION);
  GET_WIDGET (app->add_effect, "add_effect", GTK_ACTION);
  GET_WIDGET (app->add_title, "add_text", GTK_ACTION);
  GET_WIDGET (app->add_test, "add_test", GTK_ACTION);
  GET_WIDGET (app->add_transition, "add_transition", GTK_ACTION);
  GET_WIDGET (app->delete, "delete", GTK_ACTION);
  GET_WIDGET (app->play, "play", GTK_ACTION);
  GET_WIDGET (app->stop, "stop", GTK_ACTION);
  GET_WIDGET (app->move_up, "move_up", GTK_ACTION);
  GET_WIDGET (app->move_down, "move_down", GTK_ACTION);
  GET_WIDGET (app->seconds, "seconds", GTK_ENTRY);
  GET_WIDGET (app->generic_duration, "generic_duration", GTK_WIDGET);
  GET_WIDGET (app->background_type, "background_type", GTK_COMBO_BOX);
  GET_WIDGET (app->background_properties, "background_properties", GTK_WIDGET);
  GET_WIDGET (app->frequency, "frequency", GTK_SPIN_BUTTON);
  GET_WIDGET (app->volume, "volume", GTK_HSCALE);
  GET_WIDGET (app->audio_track_action, "audio_track", GTK_TOGGLE_ACTION);
  GET_WIDGET (app->video_track_action, "video_track", GTK_TOGGLE_ACTION);

  /* get text notifications */

  g_signal_connect (app->text, "notify::text",
      G_CALLBACK (text_notify_text_changed_cb), app);

  g_signal_connect (app->seconds, "notify::text",
      G_CALLBACK (seconds_notify_text_changed_cb), app);

  /* we care when the tree selection changes */

  if (!(app->selection = gtk_tree_view_get_selection (timeline)))
    goto fail;

  gtk_tree_selection_set_mode (app->selection, GTK_SELECTION_MULTIPLE);

  g_signal_connect (app->selection, "changed",
      G_CALLBACK (app_selection_changed_cb), app);

  /* create the model for the treeview */

  if (!(app->model =
          gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_OBJECT)))
    goto fail;

  gtk_tree_view_set_model (timeline, GTK_TREE_MODEL (app->model));

  /* register custom cell data function */

  gtk_tree_view_column_set_cell_data_func (duration_col, duration_renderer,
      duration_cell_func, NULL, NULL);

  /* initialize combo boxes */

  if (!(backgrounds = get_video_patterns ()))
    goto fail;

  if (!(background_type_renderer = gtk_cell_renderer_text_new ()))
    goto fail;

  gtk_combo_box_set_model (app->background_type, (GtkTreeModel *)
      backgrounds);

  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (app->background_type),
      background_type_renderer, FALSE);

  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (app->background_type),
      background_type_renderer, "text", 0);

  g_signal_connect (app->timeline, "layer-added", G_CALLBACK
      (layer_added_cb), app);
  g_signal_connect (app->timeline, "track-added", G_CALLBACK
      (track_added_cb), app);
  g_signal_connect (app->timeline, "track-removed", G_CALLBACK
      (track_removed_cb), app);

  /* register callbacks on GES objects */
  bus = gst_pipeline_get_bus (GST_PIPELINE (app->pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), app);

  /* success */
  gtk_builder_connect_signals (builder, app);
  g_object_unref (G_OBJECT (builder));
  return TRUE;

fail:
  g_object_unref (G_OBJECT (builder));
  return FALSE;
}

#undef GET_WIDGET

/* application methods ******************************************************/

static void selection_foreach (GtkTreeModel * model, GtkTreePath * path,
    GtkTreeIter * iter, gpointer user);

static void
app_toggle_playpause (App * app)
{
  if (app->state != GST_STATE_PLAYING) {
    gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PLAYING);
  } else {
    gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PAUSED);
  }
}

static void
app_stop_playback (App * app)
{
  if ((app->state != GST_STATE_NULL) && (app->state != GST_STATE_READY)) {
    gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_READY);
  }
}

typedef struct
{
  GList *objects;
  guint n;
} select_info;

static void
app_update_selection (App * app)
{
  GList *cur;
  GType type;
  select_info info = { NULL, 0 };

  /* clear old selection */
  for (cur = app->selected_objects; cur; cur = cur->next) {
    disconnect_from_object (cur->data, app);
    g_object_unref (cur->data);
    cur->data = NULL;
  }
  g_list_free (app->selected_objects);
  app->selected_objects = NULL;
  app->n_selected = 0;

  /* get new selection */
  gtk_tree_selection_selected_foreach (GTK_TREE_SELECTION (app->selection),
      selection_foreach, &info);
  app->selected_objects = info.objects;
  app->n_selected = info.n;

  type = G_TYPE_NONE;
  if (app->selected_objects) {
    type = G_TYPE_FROM_INSTANCE (app->selected_objects->data);
    for (cur = app->selected_objects; cur; cur = cur->next) {
      if (type != G_TYPE_FROM_INSTANCE (cur->data)) {
        type = G_TYPE_NONE;
        break;
      }
    }
  }

  if (type != G_TYPE_NONE) {
    for (cur = app->selected_objects; cur; cur = cur->next) {
      connect_to_object (cur->data, app);
    }
  }

  app->selected_type = type;
  app_update_first_last_selected (app);
}

static void
selection_foreach (GtkTreeModel * model, GtkTreePath * path, GtkTreeIter
    * iter, gpointer user)
{
  select_info *info = (select_info *) user;
  GESClip *obj;

  gtk_tree_model_get (model, iter, 2, &obj, -1);
  info->objects = g_list_append (info->objects, obj);

  info->n++;
  return;
}

static GList *
app_get_selected_objects (App * app)
{
  return g_list_copy (app->selected_objects);
}

static void
app_delete_objects (App * app, GList * objects)
{
  GList *cur;

  for (cur = objects; cur; cur = cur->next) {
    ges_timeline_layer_remove_object (app->layer, GES_CLIP (cur->data));
    cur->data = NULL;
  }

  g_list_free (objects);
}

/* the following two methods assume exactly one clip is selected and that the
 * requested action is valid */

static void
app_move_selected_up (App * app)
{
  GList *objects, *tmp;
  gint pos;

  objects = ges_timeline_layer_get_objects (app->layer);
  pos = g_list_index (objects, app->selected_objects->data);

  ges_simple_timeline_layer_move_object (GES_SIMPLE_TIMELINE_LAYER (app->layer),
      GES_CLIP (app->selected_objects->data), pos - 1);

  for (tmp = objects; tmp; tmp = tmp->next) {
    g_object_unref (tmp->data);
  }
}

static void
app_add_effect_on_selected_clips (App * app, const gchar * bin_desc,
    GESTrackType type)
{
  GList *objects, *tmp;
  GESTrackObject *effect = NULL;

  /* No crash if the video is playing */
  gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PAUSED);
  objects = ges_timeline_layer_get_objects (app->layer);

  for (tmp = objects; tmp; tmp = tmp->next) {
    effect = GES_TRACK_OBJECT (ges_track_parse_launch_effect_new (bin_desc));
    ges_clip_add_track_object (GES_CLIP (tmp->data), effect);

    if (type == GES_TRACK_TYPE_VIDEO)
      ges_track_add_object (app->video_track, effect);
    else if (type == GES_TRACK_TYPE_AUDIO)
      ges_track_add_object (app->audio_track, effect);

    g_object_unref (tmp->data);
  }
}

gboolean
add_effect_dlg_delete_event_cb (GtkWidget * widget, GdkEvent * event,
    gpointer * app)
{
  gtk_widget_hide_all (((App *) app)->add_effect_dlg);
  return TRUE;
}

void
on_cancel_add_effect_cb (GtkButton * button, App * app)
{
  gtk_widget_hide_all (app->add_effect_dlg);
}

void
on_apply_effect_cb (GtkButton * button, App * app)
{
  const gchar *effect;

  effect = gtk_entry_get_text (GTK_ENTRY (app->video_effect_entry));
  if (g_strcmp0 (effect, ""))
    app_add_effect_on_selected_clips (app, effect, GES_TRACK_TYPE_VIDEO);

  gtk_entry_set_text (GTK_ENTRY (app->video_effect_entry), "");

  effect = gtk_entry_get_text (GTK_ENTRY (app->audio_effect_entry));
  if (g_strcmp0 (effect, ""))
    app_add_effect_on_selected_clips (app, effect, GES_TRACK_TYPE_AUDIO);

  gtk_entry_set_text (GTK_ENTRY (app->audio_effect_entry), "");

  gtk_widget_hide_all (app->add_effect_dlg);
}

static void
app_move_selected_down (App * app)
{
  GList *objects, *tmp;
  gint pos;

  objects = ges_timeline_layer_get_objects (app->layer);
  pos = g_list_index (objects, app->selected_objects->data);

  ges_simple_timeline_layer_move_object (GES_SIMPLE_TIMELINE_LAYER (app->layer),
      GES_CLIP (app->selected_objects->data), pos - 1);

  for (tmp = objects; tmp; tmp = tmp->next) {
    g_object_unref (tmp->data);
  }
}

static void
app_add_file (App * app, gchar * uri)
{
  GESClip *obj;

  GST_DEBUG ("adding file %s", uri);

  obj = GES_CLIP (ges_uri_clip_new (uri));

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (app->layer),
      obj, -1);
}

static void
app_launch_project (App * app, gchar * uri)
{
  GESTimeline *timeline;
  GMainLoop *mainloop;
  GESTimelinePipeline *pipeline;
  GstBus *bus;
  GESFormatter *formatter;

  uri = g_strsplit (uri, "//", 2)[1];
  printf ("we will launch this uri : %s\n", uri);
  formatter = GES_FORMATTER (ges_pitivi_formatter_new ());
  timeline = ges_timeline_new ();
  pipeline = ges_timeline_pipeline_new ();
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  mainloop = g_main_loop_new (NULL, FALSE);

  ges_timeline_pipeline_add_timeline (pipeline, timeline);
  ges_formatter_load_from_uri (formatter, timeline, uri, NULL);
  ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_PREVIEW_VIDEO);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (project_bus_message_cb),
      mainloop);
  g_main_loop_run (mainloop);
}

static void
app_add_title (App * app)
{
  GESClip *obj;

  GST_DEBUG ("adding title");

  obj = GES_CLIP (ges_timeline_title_source_new ());
  g_object_set (G_OBJECT (obj), "duration", GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (app->layer),
      obj, -1);
}

static void
app_add_test (App * app)
{
  GESClip *obj;

  GST_DEBUG ("adding test");

  obj = GES_CLIP (ges_timeline_test_source_new ());
  g_object_set (G_OBJECT (obj), "duration", GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
      (app->layer), obj, -1);
}

static void
app_add_transition (App * app)
{
  GESClip *obj;

  GST_DEBUG ("adding transition");

  obj = GES_CLIP (ges_standard_transition_clip_new
      (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE));
  g_object_set (G_OBJECT (obj), "duration", GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
      (app->layer), obj, -1);
}

static void
app_save_to_uri (App * app, gchar * uri)
{
  ges_timeline_save_to_uri (app->timeline, uri, NULL, FALSE, NULL);
}

static void
app_add_audio_track (App * app)
{
  if (app->audio_tracks)
    return;

  app->audio_track = ges_track_audio_raw_new ();
  ges_timeline_add_track (app->timeline, app->audio_track);
}

static void
app_remove_audio_track (App * app)
{
  if (!app->audio_tracks)
    return;

  ges_timeline_remove_track (app->timeline, app->audio_track);
  app->audio_track = NULL;
}

static void
app_add_video_track (App * app)
{
  if (app->video_tracks)
    return;

  app->video_track = ges_track_video_raw_new ();
  ges_timeline_add_track (app->timeline, app->video_track);
}

static void
app_remove_video_track (App * app)
{
  if (!app->video_tracks)
    return;

  ges_timeline_remove_track (app->timeline, app->video_track);
  app->video_track = NULL;
}

static void
app_dispose (App * app)
{
  if (app) {
    if (app->pipeline) {
      gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_NULL);
      gst_object_unref (app->pipeline);
    }

    g_free (app);
  }

  n_instances--;

  if (n_instances == 0) {
    gtk_main_quit ();
  }
}

static App *
app_init (void)
{
  App *ret;
  ret = g_new0 (App, 1);
  n_instances++;

  ret->selected_type = G_TYPE_NONE;

  if (!ret)
    return NULL;

  if (!(ret->timeline = ges_timeline_new ()))
    goto fail;

  if (!(ret->pipeline = ges_timeline_pipeline_new ()))
    goto fail;

  if (!ges_timeline_pipeline_add_timeline (ret->pipeline, ret->timeline))
    goto fail;

  if (!(create_ui (ret)))
    goto fail;

  return ret;

fail:
  app_dispose (ret);
  return NULL;
}

static App *
app_new (void)
{
  App *ret;
  GESTrack *a = NULL, *v = NULL;

  ret = app_init ();

  /* add base audio and video track */

  if (!(a = ges_track_audio_raw_new ()))
    goto fail;

  if (!(ges_timeline_add_track (ret->timeline, a)))
    goto fail;

  if (!(v = ges_track_video_raw_new ()))
    goto fail;

  if (!(ges_timeline_add_track (ret->timeline, v)))
    goto fail;

  if (!(ret->layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ()))
    goto fail;

  if (!(ges_timeline_add_layer (ret->timeline, ret->layer)))
    goto fail;

  ret->audio_track = a;
  ret->video_track = v;
  return ret;

fail:

  if (a)
    gst_object_unref (a);
  if (v)
    gst_object_unref (v);
  app_dispose (ret);
  return NULL;
}

static gboolean
load_file_async (App * app)
{
  ges_timeline_load_from_uri (app->timeline, app->pending_uri, NULL);

  g_free (app->pending_uri);
  app->pending_uri = NULL;

  return FALSE;
}

static gboolean
app_new_from_uri (gchar * uri)
{
  App *ret;

  ret = app_init ();
  ret->pending_uri = g_strdup (uri);
  g_idle_add ((GSourceFunc) load_file_async, ret);

  return FALSE;
}

/* UI callbacks  ************************************************************/

gboolean
window_delete_event_cb (GtkObject * window, GdkEvent * event, App * app)
{
  app_dispose (app);
  return FALSE;
}

void
new_activate_cb (GtkMenuItem * item, App * app)
{
  app_new ();
}

void
launch_pitivi_project_activate_cb (GtkMenuItem * item, App * app)
{
  GtkFileChooserDialog *dlg;
  GtkFileFilter *filter;

  GST_DEBUG ("add file signal handler");

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "pitivi projects");
  gtk_file_filter_add_pattern (filter, "*.xptv");
  dlg = (GtkFileChooserDialog *)
      gtk_file_chooser_dialog_new ("Preview Project...",
      GTK_WINDOW (app->main_window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dlg), filter);

  g_object_set (G_OBJECT (dlg), "select-multiple", FALSE, NULL);

  if (gtk_dialog_run ((GtkDialog *) dlg) == GTK_RESPONSE_OK) {
    gchar *uri;
    uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dlg));
    gtk_widget_destroy ((GtkWidget *) dlg);
    app_launch_project (app, uri);
  }
}

void
open_activate_cb (GtkMenuItem * item, App * app)
{
  GtkFileChooserDialog *dlg;

  GST_DEBUG ("add file signal handler");

  dlg = (GtkFileChooserDialog *) gtk_file_chooser_dialog_new ("Open Project...",
      GTK_WINDOW (app->main_window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

  g_object_set (G_OBJECT (dlg), "select-multiple", FALSE, NULL);

  if (gtk_dialog_run ((GtkDialog *) dlg) == GTK_RESPONSE_OK) {
    gchar *uri;
    uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dlg));
    app_new_from_uri (uri);
    g_free (uri);
  }
  gtk_widget_destroy ((GtkWidget *) dlg);
}

void
save_as_activate_cb (GtkMenuItem * item, App * app)
{
  GtkFileChooserDialog *dlg;

  GST_DEBUG ("save as signal handler");

  dlg = (GtkFileChooserDialog *)
      gtk_file_chooser_dialog_new ("Save project as...",
      GTK_WINDOW (app->main_window), GTK_FILE_CHOOSER_ACTION_SAVE,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_OK,
      NULL);

  g_object_set (G_OBJECT (dlg), "select-multiple", FALSE, NULL);

  if (gtk_dialog_run ((GtkDialog *) dlg) == GTK_RESPONSE_OK) {
    gchar *uri;
    uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dlg));
    app_save_to_uri (app, uri);
    g_free (uri);
  }
  gtk_widget_destroy ((GtkWidget *) dlg);
}

void
quit_item_activate_cb (GtkMenuItem * item, App * app)
{
  gtk_main_quit ();
}

void
delete_activate_cb (GtkAction * item, App * app)
{
  /* get a gslist of selected track objects */
  GList *objects = NULL;

  objects = app_get_selected_objects (app);
  app_delete_objects (app, objects);
}

void
add_effect_activate_cb (GtkAction * item, App * app)
{
  gtk_widget_show_all (app->add_effect_dlg);
}

void
add_file_activate_cb (GtkAction * item, App * app)
{
  GtkFileChooserDialog *dlg;

  GST_DEBUG ("add file signal handler");

  dlg = (GtkFileChooserDialog *) gtk_file_chooser_dialog_new ("Add File...",
      GTK_WINDOW (app->main_window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

  g_object_set (G_OBJECT (dlg), "select-multiple", TRUE, NULL);

  if (gtk_dialog_run ((GtkDialog *) dlg) == GTK_RESPONSE_OK) {
    GSList *uris;
    GSList *cur;
    uris = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dlg));
    for (cur = uris; cur; cur = cur->next)
      app_add_file (app, cur->data);
    g_slist_free (uris);
  }
  gtk_widget_destroy ((GtkWidget *) dlg);
}

void
add_text_activate_cb (GtkAction * item, App * app)
{
  app_add_title (app);
}

void
add_test_activate_cb (GtkAction * item, App * app)
{
  app_add_test (app);
}

void
add_transition_activate_cb (GtkAction * item, App * app)
{
  app_add_transition (app);
}

void
play_activate_cb (GtkAction * item, App * app)
{
  app_toggle_playpause (app);
}

void
stop_activate_cb (GtkAction * item, App * app)
{
  app_stop_playback (app);
}

void
move_up_activate_cb (GtkAction * item, App * app)
{
  app_move_selected_up (app);
}

void
move_down_activate_cb (GtkAction * item, App * app)
{
  app_move_selected_down (app);
}

void
audio_track_activate_cb (GtkToggleAction * item, App * app)
{
  if (gtk_toggle_action_get_active (item)) {
    app_add_audio_track (app);
  } else {
    app_remove_audio_track (app);
  }
}

void
video_track_activate_cb (GtkToggleAction * item, App * app)
{
  if (gtk_toggle_action_get_active (item)) {
    app_add_video_track (app);
  } else {
    app_remove_video_track (app);
  }
}

void
app_selection_changed_cb (GtkTreeSelection * selection, App * app)
{
  app_update_selection (app);

  update_delete_sensitivity (app);
  update_effect_sensitivity (app);
  update_add_transition_sensitivity (app);
  update_move_up_down_sensitivity (app);

  gtk_widget_set_visible (app->properties, app->n_selected > 0);

  gtk_widget_set_visible (app->filesource_properties,
      app->selected_type == GES_TYPE_URI_CLIP);

  gtk_widget_set_visible (app->text_properties,
      app->selected_type == GES_TYPE_TIMELINE_TITLE_SOURCE);

  gtk_widget_set_visible (app->generic_duration,
      app->selected_type != G_TYPE_NONE &&
      app->selected_type != G_TYPE_INVALID);

  gtk_widget_set_visible (app->background_properties,
      app->selected_type == GES_TYPE_TIMELINE_TEST_SOURCE);
}

gboolean
duration_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  GList *i;

  for (i = app->selected_objects; i; i = i->next) {
    guint64 duration, maxduration;
    maxduration = GES_TIMELINE_ELEMENT_MAX_DURATION (i->data);
    duration = (value < maxduration ? (value > 0 ? value : 0) : maxduration);
    g_object_set (G_OBJECT (i->data), "duration", (guint64) duration, NULL);
  }
  return TRUE;
}

gboolean
in_point_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  GList *i;

  for (i = app->selected_objects; i; i = i->next) {
    guint64 in_point, maxduration;
    maxduration = GES_TIMELINE_ELEMENT_MAX_DURATION (i->data) -
        GES_TIMELINE_ELEMENT_DURATION (i->data);
    in_point = (value < maxduration ? (value > 0 ? value : 0) : maxduration);
    g_object_set (G_OBJECT (i->data), "in-point", (guint64) in_point, NULL);
  }
  return TRUE;
}

void
halign_changed_cb (GtkComboBox * widget, App * app)
{
  GList *tmp;
  int active;

  if (app->ignore_input)
    return;

  active = gtk_combo_box_get_active (app->halign);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "halignment", active, NULL);
  }
}

void
valign_changed_cb (GtkComboBox * widget, App * app)
{
  GList *tmp;
  int active;

  if (app->ignore_input)
    return;

  active = gtk_combo_box_get_active (app->valign);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "valignment", active, NULL);
  }
}

void
background_type_changed_cb (GtkComboBox * widget, App * app)
{
  GList *tmp;
  gint p;

  if (app->ignore_input)
    return;

  p = gtk_combo_box_get_active (widget);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "vpattern", (gint) p, NULL);
  }
}

void
frequency_value_changed_cb (GtkSpinButton * widget, App * app)
{
  GList *tmp;
  gdouble value;

  if (app->ignore_input)
    return;

  value = gtk_spin_button_get_value (widget);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "freq", (gdouble) value, NULL);
  }
}

gboolean
volume_change_value_cb (GtkRange * widget, GtkScrollType unused, gdouble
    value, App * app)
{
  GList *tmp;

  value = value >= 0 ? (value <= 2.0 ? value : 2.0) : 0;

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "volume", (gdouble) value, NULL);
  }
  return TRUE;
}

/* main *********************************************************************/

int
main (int argc, char *argv[])
{
  App *app;

  /* intialize GStreamer and GES */
  gst_init (&argc, &argv);
  ges_init ();

  /* initialize UI */
  gtk_init (&argc, &argv);

  if ((app = app_new ())) {
    gtk_main ();
  }

  return 0;
}
