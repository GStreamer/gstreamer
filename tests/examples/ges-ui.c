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
  GtkHScale *duration;
} App;

App *app_new (void);

void app_dispose (App * app);

void app_add_file (App * app, gchar * filename);

GList *app_get_selected_objects (App * app);

void app_delete_objects (App * app, GList * objects);

void app_update_selection (App * app);

void window_destroy_cb (GtkObject * window, App * app);

void quit_item_activate_cb (GtkMenuItem * item, App * app);

void delete_item_activate_cb (GtkMenuItem * item, App * app);

void add_file_item_activate_cb (GtkMenuItem * item, App * app);

void app_selection_changed_cb (GtkTreeSelection * selection, App * app);

gboolean duration_scale_change_value_cb (GtkRange * range, GtkScrollType
    unused, gdouble value, App * app);

gboolean create_ui (App * app);

static gboolean find_row_for_object (GtkListStore * model, GtkTreeIter * ret,
    GESTimelineObject * object);

void timeline_object_notify_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app);

void filesource_notify_duration_cb (GESTimelineObject * object, GParamSpec *
    arg G_GNUC_UNUSED, App * app);

void filesource_notify_max_duration_cb (GESTimelineObject * object, GParamSpec *
    arg G_GNUC_UNUSED, App * app);

void connect_to_filesource (GESTimelineObject * object, App * app);

void disconnect_from_filesource (GESTimelineObject * object, App * app);

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
delete_item_activate_cb (GtkMenuItem * item, App * app)
{
  /* get a gslist of selected track objects */
  GList *objects = NULL;

  objects = app_get_selected_objects (app);
  app_delete_objects (app, objects);
}

void
add_file_item_activate_cb (GtkMenuItem * item, App * app)
{
  GST_DEBUG ("add file signal handler");

  /* TODO: solicit this information from the user */
  app_add_file (app, (gchar *) "/home/brandon/media/small-mvi_0008.avi");
}

void
app_selection_changed_cb (GtkTreeSelection * selection, App * app)
{
  app_update_selection (app);

  /* some widgets should be disabled when we have an empty selection */
  gtk_widget_set_sensitive (app->properties, app->n_selected > 0);
}

gboolean
duration_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  GList *i;

  for (i = app->selected_objects; i; i = i->next) {
    /* this signal is called *before* the widget is updated. by returing TRUE
     * we prevent further processing. the scale value is set in
     * filesource_notify_duration_cb */
    g_object_set (G_OBJECT (i->data), "duration", (guint64) value, NULL);
  }
  return TRUE;
}

/* application methods ******************************************************/

static void selection_foreach (GtkTreeModel * model, GtkTreePath * path,
    GtkTreeIter * iter, gpointer user);

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
app_add_file (App * app, gchar * filename)
{
  gchar *uri;
  GESTimelineObject *obj;

  GST_DEBUG ("adding file %s", filename);

  if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
    /* TODO: error notification in UI */
    return;
  }
  uri = g_strdup_printf ("file://%s", filename);

  obj = GES_TIMELINE_OBJECT (ges_timeline_filesource_new (uri));
  g_free (uri);

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

static gchar *
desc_for_object (GESTimelineObject * object)
{
  gchar *uri;

  /* there is only one type of object at the moment */

  /* return the uri */
  g_object_get (object, "uri", &uri, NULL);
  return uri;
}

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

  gtk_list_store_remove (app->model, &iter);
}

/* this callback is registered for every timeline object, and updates the
 * corresponding duration cell in the model */
void
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

void
filesource_notify_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  gtk_range_set_value (GTK_RANGE (app->duration),
      GES_TIMELINE_OBJECT_DURATION (object));
}

void
filesource_notify_max_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  g_print ("got here");
  gtk_range_set_range (GTK_RANGE (app->duration),
      0, (gdouble) GES_TIMELINE_FILE_SOURCE (object)->maxduration);
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
  g_signal_connect (G_OBJECT (object), "notify::duration",
      G_CALLBACK (filesource_notify_duration_cb), app);
  filesource_notify_duration_cb (object, NULL, app);

  g_signal_connect (G_OBJECT (object), "notify::max-duration",
      G_CALLBACK (filesource_notify_max_duration_cb), app);
  filesource_notify_max_duration_cb (object, NULL, app);
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

  /* construct widget tree */

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);
  gtk_builder_connect_signals (builder, app);

  /* get a bunch of widgets from the XML tree */

  GET_WIDGET (timeline, "timeline_treeview", GTK_TREE_VIEW);
  GET_WIDGET (app->properties, "properties", GTK_WIDGET);
  GET_WIDGET (app->main_window, "window", GTK_WIDGET);
  GET_WIDGET (app->duration, "duration_scale", GTK_HSCALE);

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

  /* register callbacks on GES objects */

  g_signal_connect (app->layer, "object-added",
      G_CALLBACK (layer_object_added_cb), app);
  g_signal_connect (app->layer, "object-removed",
      G_CALLBACK (layer_object_removed_cb), app);

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
