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
} App;

App *app_new (void);

void app_dispose (App * app);

void app_add_file (App * app, gchar * filename);

void window_destroy_cb (GtkObject * window, App * app);

void quit_item_activate_cb (GtkMenuItem * item, App * app);

void delete_item_activate_cb (GtkMenuItem * item, App * app);

void add_file_item_activate_cb (GtkMenuItem * item, App * app);

GtkWidget *create_ui (App * app);

/* signal handlers **********************************************************/

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
  g_print ("beleted!");
}

void
add_file_item_activate_cb (GtkMenuItem * item, App * app)
{
  GST_DEBUG ("add file signal handler");

  /* TODO: solicit this information from the user */
  app_add_file (app, (gchar *) "/home/brandon/media/small-mvi_0008.avi");
}

/* application methods ******************************************************/

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

  if (!(ret->main_window = create_ui (ret)))
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

/* Layout *******************************************************************/

GtkWidget *
create_ui (App * app)
{
  GtkBuilder *builder;
  GtkWidget *window;

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);
  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  gtk_builder_connect_signals (builder, app);
  g_object_unref (G_OBJECT (builder));

  gtk_widget_show (window);

  return window;
}

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
