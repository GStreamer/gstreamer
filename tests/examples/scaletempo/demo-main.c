/* main.c
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

#include "demo-player.h"
#include "demo-gui.h"

extern GOptionGroup *gtk_get_option_group (gboolean);
extern GOptionGroup *gst_init_get_option_group (void);

static void
handle_player_error (DemoPlayer * player, const gchar * msg, gpointer unused)
{
  g_print ("PLAYER ERROR: %s\n", msg);
}

static void
handle_gui_error (DemoPlayer * player, const gchar * msg, gpointer unused)
{
  g_print ("GUI ERROR: %s\n", msg);
}

static void
handle_quit (gpointer source, gpointer data)
{
  g_main_loop_quit ((GMainLoop *) data);
}


int
main (int argc, char *argv[])
{
  DemoGui *gui;
  DemoPlayer *player;
  gchar **uris = NULL;
  GOptionContext *ctx;
  GError *err = NULL;
  GMainLoop *loop;

  const GOptionEntry entries[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &uris,
        "Special option that collects any remaining arguments for us"},
    {NULL,}
  };

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("uri ...");
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_group (ctx, gtk_get_option_group (FALSE));
  g_option_context_add_main_entries (ctx, entries, NULL);
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    g_error_free (err);
    return -1;
  }
  g_option_context_free (ctx);

  gui = g_object_new (DEMO_TYPE_GUI, NULL);
  player = g_object_new (DEMO_TYPE_PLAYER, NULL);
  g_signal_connect (player, "error", G_CALLBACK (handle_player_error), NULL);
  g_signal_connect (gui, "error", G_CALLBACK (handle_gui_error), NULL);
  demo_gui_set_player (gui, player);

  loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (gui, "quiting", G_CALLBACK (handle_quit), loop);

  if (uris != NULL) {
    int i, num = g_strv_length (uris);
    GList *uri_list = NULL;
    for (i = 0; i < num; i++) {
      uri_list = g_list_append (uri_list, uris[i]);
    }
    demo_gui_set_playlist (gui, uri_list);
  }
  demo_gui_show (gui);
  g_main_loop_run (loop);

  return 0;
}
