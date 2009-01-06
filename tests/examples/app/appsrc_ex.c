

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappbuffer.h>
#include <gst/app/gstappsink.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


typedef struct _App App;
struct _App
{
  GstElement *pipe;
  GstElement *src;
  GstElement *id;
  GstElement *sink;
};

App s_app;

static void dont_eat_my_chicken_wings (void *priv);

int
main (int argc, char *argv[])
{
  App *app = &s_app;
  int i;

  gst_init (&argc, &argv);

  app->pipe = gst_pipeline_new (NULL);
  g_assert (app->pipe);

  app->src = gst_element_factory_make ("appsrc", NULL);
  g_assert (app->src);
  gst_bin_add (GST_BIN (app->pipe), app->src);

  app->id = gst_element_factory_make ("identity", NULL);
  g_assert (app->id);
  gst_bin_add (GST_BIN (app->pipe), app->id);

  app->sink = gst_element_factory_make ("appsink", NULL);
  g_assert (app->sink);
  gst_bin_add (GST_BIN (app->pipe), app->sink);

  gst_element_link (app->src, app->id);
  gst_element_link (app->id, app->sink);

  gst_element_set_state (app->pipe, GST_STATE_PLAYING);

  for (i = 0; i < 10; i++) {
    GstBuffer *buf;
    void *data;

    data = malloc (100);
    memset (data, i, 100);

    buf = gst_app_buffer_new (data, 100, dont_eat_my_chicken_wings, data);
    printf ("%d: creating buffer for pointer %p, %p\n", i, data, buf);
    gst_app_src_push_buffer (GST_APP_SRC (app->src), buf);
  }

  /* push EOS */
  gst_app_src_end_of_stream (GST_APP_SRC (app->src));

  /* _is_eos() does not block and returns TRUE if there is not currently an EOS
   * to be retrieved */
  while (!gst_app_sink_is_eos (GST_APP_SINK (app->sink))) {
    GstBuffer *buf;

    /* pull the next item, this can return NULL when there is no more data and
     * EOS has been received */
    buf = gst_app_sink_pull_buffer (GST_APP_SINK (app->sink));
    printf ("retrieved buffer %p\n", buf);
    if (buf)
      gst_buffer_unref (buf);
  }
  gst_element_set_state (app->pipe, GST_STATE_NULL);

  return 0;
}

static void
dont_eat_my_chicken_wings (void *priv)
{
  printf ("freeing buffer for pointer %p\n", priv);
  free (priv);
}
