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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <ges/ges.h>

typedef struct App
{
  GESTimeline *timeline;
  GESTimelinePipeline *pipeline;
  GESTimelineLayer *layer;
  GtkWidget *main_window;
  GtkListStore *model;
  GtkTreeSelection *selection;
  GtkWidget *properties;
  GList *selected_objects;
  int n_selected;
  int n_objects;
  GtkHScale *duration;
  GtkHScale *in_point;
  GtkAction *add_file;
  GtkAction *delete;
  GtkAction *play;
  GstState state;
} App;

App *app_new (void);

void app_dispose (App * app);

void app_add_file (App * app, gchar * filename);

GList *app_get_selected_objects (App * app);

void app_delete_objects (App * app, GList * objects);

void app_update_selection (App * app);

void window_destroy_cb (GtkObject * window, App * app);

void quit_item_activate_cb (GtkMenuItem * item, App * app);

void delete_activate_cb (GtkAction * item, App * app);

void play_activate_cb (GtkAction * item, App * app);

void add_file_activate_cb (GtkAction * item, App * app);

void app_selection_changed_cb (GtkTreeSelection * selection, App * app);

void app_toggle_playback (App * app);

gboolean duration_scale_change_value_cb (GtkRange * range, GtkScrollType
    unused, gdouble value, App * app);

gboolean in_point_scale_change_value_cb (GtkRange * range, GtkScrollType
    unused, gdouble value, App * app);

void duration_cell_func (GtkTreeViewColumn * column, GtkCellRenderer * renderer,
    GtkTreeModel * model, GtkTreeIter * iter, gpointer user);

gboolean create_ui (App * app);

void connect_to_filesource (GESTimelineObject * object, App * app);

void disconnect_from_filesource (GESTimelineObject * object, App * app);

/* UI state functions *******************************************************/

static void
update_properties_sensitivity (App * app)
{
  gtk_widget_set_sensitive (app->properties,
      (app->n_selected == 1) && (app->state != GST_STATE_PLAYING));
}

static void
update_delete_sensitivity (App * app)
{
  /* delete will work for multiple items */
  gtk_action_set_sensitive (app->delete,
      (app->n_selected > 0) && (app->state != GST_STATE_PLAYING));
}

/* UI callbacks  ************************************************************/

void
window_destroy_cb (GtkObject * window, App * app)
{
  gtk_main_quit ();
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
play_activate_cb (GtkAction * item, App * app)
{
  app_toggle_playback (app);
}

void
app_selection_changed_cb (GtkTreeSelection * selection, App * app)
{
  app_update_selection (app);

  /* doesn't make sense to set properties on more than one item */
  update_properties_sensitivity (app);
  update_delete_sensitivity (app);
}

gboolean
duration_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  GList *i;

  for (i = app->selected_objects; i; i = i->next) {
    guint64 duration, maxduration;
    maxduration = GES_TIMELINE_FILE_SOURCE (i->data)->maxduration;
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
    maxduration = GES_TIMELINE_FILE_SOURCE (i->data)->maxduration -
        GES_TIMELINE_OBJECT_DURATION (i->data);
    in_point = (value < maxduration ? (value > 0 ? value : 0) : maxduration);
    g_object_set (G_OBJECT (i->data), "in-point", (guint64) in_point, NULL);
  }
  return TRUE;
}

void
duration_cell_func (GtkTreeViewColumn * column, GtkCellRenderer * renderer,
    GtkTreeModel * model, GtkTreeIter * iter, gpointer user)
{
  gchar buf[30];
  guint64 duration;

  gtk_tree_model_get (model, iter, 1, &duration, -1);
  g_snprintf (buf, sizeof (buf), "%u:%02u:%02u.%09u", GST_TIME_ARGS (duration));
  g_object_set (renderer, "text", &buf, NULL);
}

/* application methods ******************************************************/

static void selection_foreach (GtkTreeModel * model, GtkTreePath * path,
    GtkTreeIter * iter, gpointer user);

void
app_toggle_playback (App * app)
{
  if (app->state != GST_STATE_PLAYING) {
    gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PLAYING);
  } else {
    gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PAUSED);
  }
}

void
app_update_selection (App * app)
{
  GList *cur;

  /* clear old selection */
  for (cur = app->selected_objects; cur; cur = cur->next) {
    disconnect_from_filesource (cur->data, app);
    g_object_unref (cur->data);
    cur->data = NULL;
  }
  g_list_free (app->selected_objects);
  app->selected_objects = NULL;
  app->n_selected = 0;

  /* get new selection */
  gtk_tree_selection_selected_foreach (GTK_TREE_SELECTION (app->selection),
      selection_foreach, &app->selected_objects);

  for (cur = app->selected_objects; cur; cur = cur->next) {
    connect_to_filesource (cur->data, app);
    app->n_selected++;
  }
}

static void
selection_foreach (GtkTreeModel * model, GtkTreePath * path, GtkTreeIter
    * iter, gpointer user)
{
  GList **ret = user;
  GESTimelineObject *obj;

  gtk_tree_model_get (model, iter, 2, &obj, -1);
  *ret = g_list_append (*ret, obj);

  return;
}

GList *
app_get_selected_objects (App * app)
{
  return g_list_copy (app->selected_objects);
}

void
app_delete_objects (App * app, GList * objects)
{
  GList *cur;

  for (cur = objects; cur; cur = cur->next) {
    ges_timeline_layer_remove_object (app->layer,
        GES_TIMELINE_OBJECT (cur->data));
    cur->data = NULL;
  }

  g_list_free (objects);
}

void
app_add_file (App * app, gchar * uri)
{
  GESTimelineObject *obj;

  GST_DEBUG ("adding file %s", uri);

  obj = GES_TIMELINE_OBJECT (ges_timeline_filesource_new (uri));

  ges_timeline_layer_add_object (app->layer, obj);
}

App *
app_new (void)
{
  App *ret;
  ret = g_new0 (App, 1);

  if (!ret)
    return NULL;

  if (!(ret->timeline = ges_timeline_new_audio_video ()))
    goto fail;

  if (!(ret->pipeline = ges_timeline_pipeline_new ()))
    goto fail;

  if (!ges_timeline_pipeline_add_timeline (ret->pipeline, ret->timeline))
    goto fail;

  if (!(ret->layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ()))
    goto fail;

  if (!(ges_timeline_add_layer (ret->timeline, ret->layer)))
    goto fail;

  if (!(create_ui (ret)))
    goto fail;

  return ret;

fail:
  app_dispose (ret);
  return NULL;
}

void
app_dispose (App * app)
{
  if (app) {
    if (app->pipeline)
      gst_object_unref (app->pipeline);

    g_free (app);
  }
}

/* Backend callbacks ********************************************************/

static gboolean
find_row_for_object (GtkListStore * model, GtkTreeIter * ret,
    GESTimelineObject * object)
{
  gtk_tree_model_get_iter_first ((GtkTreeModel *) model, ret);

  while (gtk_list_store_iter_is_valid (model, ret)) {
    GESTimelineObject *obj;
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
timeline_object_notify_duration_cb (GESTimelineObject * object,
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
filesource_notify_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  guint64 duration, max_inpoint;
  duration = GES_TIMELINE_OBJECT_DURATION (object);
  max_inpoint = GES_TIMELINE_FILE_SOURCE (object)->maxduration - duration;

  gtk_range_set_value (GTK_RANGE (app->duration), duration);
  gtk_range_set_fill_level (GTK_RANGE (app->in_point), max_inpoint);

  if (max_inpoint < GES_TIMELINE_OBJECT_INPOINT (object))
    g_object_set (object, "in-point", max_inpoint, NULL);

}

static void
filesource_notify_max_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  gtk_range_set_range (GTK_RANGE (app->duration),
      0, (gdouble) GES_TIMELINE_FILE_SOURCE (object)->maxduration);
  gtk_range_set_range (GTK_RANGE (app->in_point),
      0, (gdouble) GES_TIMELINE_FILE_SOURCE (object)->maxduration);
}

static void
filesource_notify_in_point_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  gtk_range_set_value (GTK_RANGE (app->in_point),
      GES_TIMELINE_OBJECT_INPOINT (object));
}

static gchar *
desc_for_object (GESTimelineObject * object)
{
  gchar *uri;

  /* there is only one type of object at the moment */

  /* return the uri */
  g_object_get (object, "uri", &uri, NULL);
  return uri;
}

static void
object_count_changed (App * app)
{
  gtk_action_set_sensitive (app->play, app->n_objects > 0);
}

static void
layer_object_added_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    App * app)
{
  GtkTreeIter iter;
  gchar *description;

  GST_INFO ("layer object added cb %p %p %p", layer, object, app);

  description = desc_for_object (object);

  gtk_list_store_append (app->model, &iter);
  gtk_list_store_set (app->model, &iter, 0, description, 2, object, -1);

  g_signal_connect (G_OBJECT (object), "notify::duration",
      G_CALLBACK (timeline_object_notify_duration_cb), app);
  timeline_object_notify_duration_cb (object, NULL, app);
  app->n_objects++;
  object_count_changed (app);
}

static void
layer_object_removed_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    App * app)
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
pipeline_state_changed_cb (App * app)
{
  if (app->state == GST_STATE_PLAYING)
    gtk_action_set_stock_id (app->play, GTK_STOCK_MEDIA_PAUSE);
  else
    gtk_action_set_stock_id (app->play, GTK_STOCK_MEDIA_PLAY);

  update_properties_sensitivity (app);
  update_delete_sensitivity (app);

  gtk_action_set_sensitive (app->add_file, app->state != GST_STATE_PLAYING);
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

/* UI Configuration *********************************************************/

#define GET_WIDGET(dest,name,type) {\
  if (!(dest =\
    type(gtk_builder_get_object(builder, name))))\
        goto fail;\
}

void
connect_to_filesource (GESTimelineObject * object, App * app)
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

void
disconnect_from_filesource (GESTimelineObject * object, App * app)
{
  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      filesource_notify_duration_cb, app);

  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      filesource_notify_max_duration_cb, app);
}

gboolean
create_ui (App * app)
{
  GtkBuilder *builder;
  GtkTreeView *timeline;
  GtkTreeViewColumn *duration_col;
  GtkCellRenderer *duration_renderer;
  GstBus *bus;

  /* construct widget tree */

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);
  gtk_builder_connect_signals (builder, app);

  /* get a bunch of widgets from the XML tree */

  GET_WIDGET (timeline, "timeline_treeview", GTK_TREE_VIEW);
  GET_WIDGET (app->properties, "properties", GTK_WIDGET);
  GET_WIDGET (app->main_window, "window", GTK_WIDGET);
  GET_WIDGET (app->duration, "duration_scale", GTK_HSCALE);
  GET_WIDGET (app->in_point, "in_point_scale", GTK_HSCALE);
  GET_WIDGET (duration_col, "duration_column", GTK_TREE_VIEW_COLUMN);
  GET_WIDGET (duration_renderer, "duration_renderer", GTK_CELL_RENDERER);
  GET_WIDGET (app->add_file, "add_file", GTK_ACTION);
  GET_WIDGET (app->delete, "delete", GTK_ACTION);
  GET_WIDGET (app->play, "play", GTK_ACTION);

  /* we care when the tree selection changes */

  if (!(app->selection = gtk_tree_view_get_selection (timeline)))
    goto fail;

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

  /* register callbacks on GES objects */

  g_signal_connect (app->layer, "object-added",
      G_CALLBACK (layer_object_added_cb), app);
  g_signal_connect (app->layer, "object-removed",
      G_CALLBACK (layer_object_removed_cb), app);

  bus = gst_pipeline_get_bus (GST_PIPELINE (app->pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), app);

  /* success */

  g_object_unref (G_OBJECT (builder));
  return TRUE;

fail:
  g_object_unref (G_OBJECT (builder));
  return FALSE;
}

#undef GET_WIDGET

/* main *********************************************************************/

int
main (int argc, char *argv[])
{
  App *app;

  /* intialize GStreamer and GES */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  gst_init (&argc, &argv);
  ges_init ();

  /* initialize UI */
  gtk_init (&argc, &argv);

  if ((app = app_new ())) {
    gtk_main ();
    app_dispose (app);
  }

  return 0;
}
