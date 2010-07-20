#include <gtk/gtk.h>
#include <glib.h>
#include <ges/ges.h>

typedef struct App
{
  GESTimeline *timeline;
  GESTimelinePipeline *pipeline;
  GtkWidget *main_window;
} App;

App *app_new (void);

void app_dispose (App * app);

void window_destroy_cb (GtkObject * window, gpointer user);

void quit_item_activate_cb (GtkMenuItem * item, gpointer user);

void delete_item_activate_cb (GtkMenuItem * item, gpointer user);

void add_file_item_activate_cb (GtkMenuItem * item, gpointer user);

GtkWidget *create_ui (App * app);

void
window_destroy_cb (GtkObject * window, gpointer user)
{
  gtk_main_quit ();
}

void
quit_item_activate_cb (GtkMenuItem * item, gpointer user)
{
  gtk_main_quit ();
}

void
delete_item_activate_cb (GtkMenuItem * item, gpointer user)
{
  g_print ("beleted!");
}

void
add_file_item_activate_cb (GtkMenuItem * item, gpointer user)
{
  g_print ("add file");
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

    if (app->timeline)
      g_object_unref (app->timeline);

    if (app->main_window)
      g_object_unref (app->main_window);

    g_free (app);
  }
}

GtkWidget *
create_ui (App * data)
{
  GtkBuilder *builder;
  GtkWidget *window;

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);
  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  gtk_builder_connect_signals (builder, NULL);
  g_object_unref (G_OBJECT (builder));

  gtk_widget_show (window);
  return window;
}

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

  app = app_new ();

  gtk_main ();

  return 0;
}
