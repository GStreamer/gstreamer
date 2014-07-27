/* GStreamer command line playback testing utility - keyboard handling helpers
 *
 * Copyright (C) 2013 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2013 Centricular Ltd
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst-play-kb.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <unistd.h>
#include <termios.h>
#endif

#include <gst/gst.h>

/* This is all not thread-safe, but doesn't have to be really */

#ifdef G_OS_UNIX

static struct termios term_settings;
static gboolean term_settings_saved = FALSE;
static GstPlayKbFunc kb_callback;
static gpointer kb_callback_data;
static gulong io_watch_id;

static gboolean
gst_play_kb_io_cb (GIOChannel * ioc, GIOCondition cond, gpointer user_data)
{
  GIOStatus status;

  if (cond & G_IO_IN) {
    gchar buf[16] = { 0, };
    gsize read;

    status = g_io_channel_read_chars (ioc, buf, sizeof (buf) - 1, &read, NULL);
    if (status == G_IO_STATUS_ERROR)
      return FALSE;
    if (status == G_IO_STATUS_NORMAL) {
      if (kb_callback)
        kb_callback (buf, kb_callback_data);
    }
  }

  return TRUE;                  /* call us again */
}

gboolean
gst_play_kb_set_key_handler (GstPlayKbFunc kb_func, gpointer user_data)
{
  GIOChannel *ioc;
  int flags;

  if (!isatty (STDIN_FILENO)) {
    GST_INFO ("stdin is not connected to a terminal");
    return FALSE;
  }

  if (io_watch_id > 0) {
    g_source_remove (io_watch_id);
    io_watch_id = 0;
  }

  if (kb_func == NULL && term_settings_saved) {
    /* restore terminal settings */
    if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &term_settings) == 0)
      term_settings_saved = FALSE;
    else
      g_warning ("could not restore terminal attributes");

    setvbuf (stdin, NULL, _IOLBF, 0);
  }

  if (kb_func != NULL) {
    struct termios new_settings;

    if (!term_settings_saved) {
      if (tcgetattr (STDIN_FILENO, &term_settings) != 0) {
        g_warning ("could not save terminal attributes");
        return FALSE;
      }
      term_settings_saved = TRUE;

      /* Echo off, canonical mode off, extended input processing off  */
      new_settings = term_settings;
      new_settings.c_lflag &= ~(ECHO | ICANON | IEXTEN);

      if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &new_settings) != 0) {
        g_warning ("Could not set terminal state");
        return FALSE;
      }
      setvbuf (stdin, NULL, _IONBF, 0);
    }
  }

  ioc = g_io_channel_unix_new (STDIN_FILENO);

  /* make non-blocking */
  flags = g_io_channel_get_flags (ioc);
  g_io_channel_set_flags (ioc, flags | G_IO_FLAG_NONBLOCK, NULL);

  io_watch_id = g_io_add_watch_full (ioc, G_PRIORITY_DEFAULT, G_IO_IN,
      (GIOFunc) gst_play_kb_io_cb, user_data, NULL);
  g_io_channel_unref (ioc);

  kb_callback = kb_func;
  kb_callback_data = user_data;

  return TRUE;
}

#else /* !G_OS_UNIX */

gboolean
gst_play_kb_set_key_handler (GstPlayKbFunc key_func, gpointer user_data)
{
  GST_FIXME ("Keyboard handling for this OS needs to be implemented");
  return FALSE;
}

#endif /* !G_OS_UNIX */
