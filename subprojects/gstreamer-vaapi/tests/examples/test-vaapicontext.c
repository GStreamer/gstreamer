/*
 *  test-vaapicontext.c - Testsuite for VAAPI app context
 *
 *  Copyright (C) 2017 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#include <va/va.h>

#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <va/va_x11.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#include <va/va_wayland.h>
#endif

static gboolean g_multisink;
static gchar *g_filepath;
static GstElement *g_vaapisink;

static GOptionEntry g_options[] = {
  {"multi", 'm', 0, G_OPTION_ARG_NONE, &g_multisink, "test multiple vaapisink",
      NULL},
  {"file", 'f', 0, G_OPTION_ARG_STRING, &g_filepath, "file path to play", NULL},
  {NULL,}
};

typedef struct _CustomData
{
  GtkWidget *main_window;
  VADisplay va_display;
  GstElement *pipeline;
  guintptr videoarea_handle[2];
  GstObject *gstvaapidisplay;
  GtkWidget *video_widget[2];
  GstVideoOverlay *overlay[2];
} AppData;

static void
delete_event_cb (GtkWidget * widget, GdkEvent * event, gpointer data)
{
  AppData *app = data;

  gst_element_set_state (app->pipeline, GST_STATE_NULL);
  gtk_main_quit ();
}

static void
button_rotate_cb (GtkWidget * widget, GstElement * elem)
{
  static gint counter = 0;
  const static gint tags[] = { 90, 180, 270, 0 };

  g_object_set (elem, "rotation", tags[counter++ % G_N_ELEMENTS (tags)], NULL);
}

static gpointer
get_native_display (AppData * app, gboolean * is_x11)
{
  GdkDisplay *gdk_display;

  gdk_display = gtk_widget_get_display (app->main_window);

#if defined(GDK_WINDOWING_X11)
  if (GDK_IS_X11_DISPLAY (gdk_display)) {
    *is_x11 = TRUE;
    return gdk_x11_display_get_xdisplay (gdk_display);
  } else
#endif
#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (gdk_display)) {
    *is_x11 = FALSE;
    return gdk_wayland_display_get_wl_display (gdk_display);
  } else
#endif
    g_error ("Running in a non supported environment");
}

static VADisplay
ensure_va_display (AppData * app, gpointer native_display, gboolean is_x11)
{
  if (app->va_display)
    return app->va_display;
  app->va_display = is_x11 ?
      vaGetDisplay (native_display) : vaGetDisplayWl (native_display);
  /* There's no need to call vaInitialize() since element does it
   * internally */
  return app->va_display;
}

static GstContext *
create_vaapi_app_display_context (AppData * app, gboolean new_va_display)
{
  GstContext *context;
  GstStructure *s;
  VADisplay va_display;
  gpointer native_display = NULL;
  const gchar *name = NULL;
  gboolean is_x11;

  native_display = get_native_display (app, &is_x11);

  if (new_va_display) {
    va_display = is_x11 ?
        vaGetDisplay (native_display) : vaGetDisplayWl (native_display);
  } else
    va_display = ensure_va_display (app, native_display, is_x11);

  name = is_x11 ? "x11-display" : "wl-display";

  context = gst_context_new ("gst.vaapi.app.Display", FALSE);
  s = gst_context_writable_structure (context);
  gst_structure_set (s, "va-display", G_TYPE_POINTER, va_display, NULL);
  gst_structure_set (s, name, G_TYPE_POINTER, native_display, NULL);

  return context;
}

static void
get_allocation (GtkWidget * widget, GtkAllocation * allocation)
{
  GdkDisplay *display = gdk_display_get_default ();

  gtk_widget_get_allocation (widget, allocation);

  /* On Wayland the whole gtk window is one surface and the video is a
   * subsurface of the top-level surface. So the position must be relative
   * to the top-level window not relative to the parent widget */
  if (GDK_IS_WAYLAND_DISPLAY (display))
    gtk_widget_translate_coordinates (widget, gtk_widget_get_toplevel (widget),
        0, 0, &allocation->x, &allocation->y);
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * msg, gpointer data)
{
  AppData *app = data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_NEED_CONTEXT:{
      const gchar *context_type;
      gboolean new_va_disp;
      GstContext *context;

      gst_message_parse_context_type (msg, &context_type);
      gst_println ("Got need context %s from %s", context_type,
          GST_MESSAGE_SRC_NAME (msg));

      if (g_strcmp0 (context_type, "gst.vaapi.Display") == 0) {
        if (app->gstvaapidisplay) {
          GstStructure *s;

          context = gst_context_new ("gst.vaapi.Display", FALSE);
          s = gst_context_writable_structure (context);
          gst_structure_set (s, "gst.vaapi.Display",
              GST_TYPE_OBJECT, app->gstvaapidisplay, NULL);
        }
        break;
      }

      if (g_strcmp0 (context_type, "gst.vaapi.app.Display") != 0)
        break;

      /* create a new VA display *only* for the second video sink */
      new_va_disp = (g_strcmp0 (GST_MESSAGE_SRC_NAME (msg), "sink2") == 0);

      context = create_vaapi_app_display_context (app, new_va_disp);
      gst_element_set_context (GST_ELEMENT (GST_MESSAGE_SRC (msg)), context);
      gst_context_unref (context);
      break;
    }
    case GST_MESSAGE_ELEMENT:{
      GstVideoOverlay *overlay;
      GtkAllocation allocation;
      guint i;

      if (!gst_is_video_overlay_prepare_window_handle_message (msg))
        break;

      overlay = GST_VIDEO_OVERLAY (GST_MESSAGE_SRC (msg));

      i = (g_strcmp0 (GST_MESSAGE_SRC_NAME (msg), "sink2") == 0) ? 1 : 0;

      app->overlay[i] = overlay;
      get_allocation (app->video_widget[i], &allocation);
      gst_video_overlay_set_window_handle (overlay, app->videoarea_handle[i]);
      gtk_widget_queue_draw_area (app->video_widget[i], 0, 0, allocation.width,
          allocation.height);

      break;
    }
    case GST_MESSAGE_HAVE_CONTEXT:{
      const gchar *context_type;
      const GstStructure *s;
      GstContext *context = NULL;
      const GValue *value;

      gst_message_parse_have_context (msg, &context);
      if (!context)
        break;

      context_type = gst_context_get_context_type (context);
      gst_println ("Got have context %s from %s", context_type,
          GST_MESSAGE_SRC_NAME (msg));

      if (g_strcmp0 (context_type, "gst.vaapi.Display") != 0)
        break;
      s = gst_context_get_structure (context);
      if (!s)
        break;
      value = gst_structure_get_value (s, "gst.vaapi.Display");
      if (!value)
        break;
      app->gstvaapidisplay = g_value_dup_object (value);
      gst_println ("found display %s", GST_OBJECT_NAME (app->gstvaapidisplay));
      break;
    }
    case GST_MESSAGE_EOS:
      gtk_main_quit ();
      break;
    default:
      break;
  }

  return GST_BUS_PASS;
}

static void
play_cb (GtkButton * button, gpointer data)
{
  AppData *app = data;

  gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
}

static void
null_cb (GtkButton * button, gpointer data)
{
  AppData *app = data;

  gst_element_set_state (app->pipeline, GST_STATE_NULL);
  app->va_display = NULL;
}


static void
realize_cb (GtkWidget * widget, gpointer data)
{
  AppData *app = data;
  GdkWindow *window;
  GdkDisplay *display;
  static guint counter = 0;

  display = gdk_display_get_default ();

#ifdef GDK_WINDOWING_WAYLAND
  /* On wayland gtk_widget_get_window() only works correctly for the
   * toplevel widget. Otherwise a new wayland surface is created but
   * never used and the video remains invisible. */
  if (GDK_IS_WAYLAND_DISPLAY (display))
    window = gtk_widget_get_window (app->main_window);
  else
#endif
    window = gtk_widget_get_window (widget);

  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstOverlay!");

#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_DISPLAY (display)) {
    app->videoarea_handle[counter++ % 2] = GDK_WINDOW_XID (window);
  } else
#endif
#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (display)) {
    app->videoarea_handle[counter++ % 2] =
        (guintptr) gdk_wayland_window_get_wl_surface (window);
  } else
#endif
    g_error ("Unsupported GDK backend");
}

static void
draw_cb (GtkWidget * widget, cairo_t * cr, gpointer data)
{
  AppData *app = data;
  GtkAllocation allocation;
  int i;

  i = (widget == app->video_widget[0]) ? 0 : 1;

  get_allocation (widget, &allocation);

  gst_println ("draw_cb[%d] x %d, y %d, w %d, h %d\n", i,
      allocation.x, allocation.y, allocation.width, allocation.height);

  if (app->overlay[i]) {
    gst_video_overlay_set_render_rectangle (app->overlay[i], allocation.x,
        allocation.y, allocation.width, allocation.height);
  }
}

static GtkWidget *
create_video_box (AppData * app)
{
  GtkWidget *video_area;

  video_area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (video_area, 640, 480);
  g_signal_connect (video_area, "realize", G_CALLBACK (realize_cb), app);
  g_signal_connect (video_area, "draw", G_CALLBACK (draw_cb), app);
  return video_area;
}

static GtkWidget *
create_rotate_button (AppData * app, const gchar * name)
{
  GtkWidget *rotate;
  GstElement *sink;

  sink = gst_bin_get_by_name (GST_BIN (app->pipeline), name);
  if (!sink && !g_strcmp0 (name, "sink1"))
    sink = g_vaapisink;
  g_assert (sink);

  rotate = gtk_button_new_with_label ("Rotate");
  g_signal_connect (rotate, "clicked", G_CALLBACK (button_rotate_cb), sink);
  if (sink != g_vaapisink)
    gst_object_unref (sink);

  return rotate;
}

static void
build_ui (AppData * app)
{
  GtkWidget *mainwin, *vbox, *pane, *bbox;

  mainwin = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (mainwin), "VAAPI display context test");
  gtk_window_set_resizable (GTK_WINDOW (mainwin), FALSE);
  g_signal_connect (mainwin, "delete-event", G_CALLBACK (delete_event_cb), app);
  app->main_window = mainwin;

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (mainwin), vbox);

  pane = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start (GTK_BOX (vbox), pane, TRUE, TRUE, 0);

  /* first video box */
  app->video_widget[0] = create_video_box (app);
  gtk_paned_pack1 (GTK_PANED (pane), app->video_widget[0], TRUE, TRUE);

  /* rotate buttons */
  bbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_box_pack_end (GTK_BOX (vbox), bbox, TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (bbox), create_rotate_button (app, "sink1"), TRUE,
      TRUE, 0);

  if (g_multisink) {
    /* second video box */
    app->video_widget[1] = create_video_box (app);
    gtk_paned_pack2 (GTK_PANED (pane), app->video_widget[1], TRUE, TRUE);

    gtk_box_pack_start (GTK_BOX (bbox), create_rotate_button (app, "sink2"),
        TRUE, TRUE, 0);
  } else {
    GtkWidget *button;

    button = gtk_button_new_with_label ("PLAYING");
    gtk_box_pack_start (GTK_BOX (bbox), button, TRUE, TRUE, 0);
    g_signal_connect (button, "clicked", G_CALLBACK (play_cb), app);

    button = gtk_button_new_with_label ("NULL");
    gtk_box_pack_start (GTK_BOX (bbox), button, TRUE, TRUE, 0);
    g_signal_connect (button, "clicked", G_CALLBACK (null_cb), app);
  }

  gtk_widget_show_all (mainwin);
}

int
main (gint argc, gchar ** argv)
{
  AppData app = { 0, };
  GstBus *bus;
  GOptionContext *ctx;
  GError *error = NULL;

  XInitThreads ();

  ctx = g_option_context_new ("- test options");
  if (!ctx)
    return -1;

  g_option_context_add_group (ctx, gtk_get_option_group (TRUE));
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_main_entries (ctx, g_options, NULL);
  if (!g_option_context_parse (ctx, &argc, &argv, NULL))
    return -1;
  g_option_context_free (ctx);

  if (g_multisink) {
    app.pipeline = gst_parse_launch ("videotestsrc ! tee name=t ! queue ! "
        "vaapisink name=sink1 t. ! queue ! vaapisink name=sink2", &error);
  } else if (!g_filepath) {
    app.pipeline = gst_parse_launch ("videotestsrc ! vaapih264enc ! "
        "vaapidecodebin ! vaapisink name=sink1", &error);
  } else {
    app.pipeline = gst_element_factory_make ("playbin", NULL);
    g_assert (app.pipeline);
  }

  if (error) {
    gst_printerrln ("failed to parse pipeline: %s", error->message);
    g_error_free (error);
    return -1;
  }

  if (!g_multisink && g_filepath) {
    g_vaapisink = gst_element_factory_make ("vaapisink", "sink1");
    g_assert (g_vaapisink);
    g_object_set (app.pipeline, "uri", g_filepath, "video-sink", g_vaapisink,
        NULL);
  }

  build_ui (&app);

  bus = gst_element_get_bus (app.pipeline);
  gst_bus_set_sync_handler (bus, bus_sync_handler, (gpointer) & app, NULL);
  gst_object_unref (bus);

  gst_element_set_state (app.pipeline, GST_STATE_PLAYING);
  gst_println ("Now playing…");

  gtk_main ();

  gst_object_unref (app.pipeline);
  gst_object_unref (app.gstvaapidisplay);
  /* there is no need to call vaTerminate() because it is done by the
   * vaapi elements */
  return 0;
}
