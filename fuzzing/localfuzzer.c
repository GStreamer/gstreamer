/* GStreamer
 * Copyright (C) 2017 Edward Hervey <bilboed@bilboed.com>
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

/* Local fuzzer runner */
#include <glib.h>

extern int LLVMFuzzerTestOneInput (const guint8 * data, size_t size);

static void
test_file (gchar * filename)
{
  GDir *dir;
  gchar *path;
  gchar *contents;
  gsize length;

  /* if filename is a directory, process the contents */
  if ((dir = g_dir_open (filename, 0, NULL))) {
    const gchar *entry;

    while ((entry = g_dir_read_name (dir))) {
      gchar *spath;

      spath = g_strconcat (filename, G_DIR_SEPARATOR_S, entry, NULL);
      test_file (spath);
      g_free (spath);
    }

    g_dir_close (dir);
    return;
  }

  /* Make sure path is absolute */
  if (!g_path_is_absolute (filename)) {
    gchar *curdir;

    curdir = g_get_current_dir ();
    path = g_build_filename (curdir, filename, NULL);
    g_free (curdir);
  } else
    path = g_strdup (filename);

  /* Check if path exists */
  if (g_file_get_contents (path, &contents, &length, NULL)) {
    g_print (">>> %s (%" G_GSIZE_FORMAT " bytes)\n", path, length);
    LLVMFuzzerTestOneInput ((const guint8 *) contents, length);
    g_free (contents);
  }

  g_free (path);
}

int
main (int argc, gchar ** argv)
{
  gint i;

  for (i = 1; i < argc; i++)
    test_file (argv[i]);

  return 0;
}
