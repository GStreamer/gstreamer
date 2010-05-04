/* GStreamer
 * Copyright (C) 2004 Thomas Vander Stichele <thomas@apestaart.org>
 *
 * gst-run.c: tool to launch GStreamer tools with correct major/minor
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <glib.h>

/* global statics for option parsing */
static gboolean _print = FALSE;
static gchar *_arg_mm = NULL;
static gboolean _arg_list_mm = FALSE;

/* popt options table for the wrapper */
static GOptionEntry wrapper_options[] = {
  {"print", 'p', 0, G_OPTION_ARG_NONE, &_print,
      "print wrapped command line options", NULL},
  {"gst-mm", 0, 0, G_OPTION_ARG_STRING, &_arg_mm,
      "Force major/minor version", "VERSION"},
  {"gst-list-mm", 0, 0, G_OPTION_ARG_NONE, &_arg_list_mm,
      "List found major/minor versions", NULL},
  {NULL}
};

/* print out the major/minor, which is the hash key */
static void
hash_print_key (gchar * key, gchar * value)
{
  g_print ("%s\n", (gchar *) key);
}

/* return value like strcmp, but compares major/minor numerically */
static gint
compare_major_minor (const gchar * first, const gchar * second)
{
  gchar **firsts, **seconds;
  gint fmaj, fmin, smaj, smin;
  gint ret = 0;

  firsts = g_strsplit (first, ".", 0);
  seconds = g_strsplit (second, ".", 0);

  if (firsts[0] == NULL || firsts[1] == NULL) {
    ret = -1;
    goto beach;
  }
  if (seconds[0] == NULL || seconds[1] == NULL) {
    ret = 1;
    goto beach;
  }

  fmaj = atoi (firsts[0]);
  fmin = atoi (firsts[1]);
  smaj = atoi (seconds[0]);
  smin = atoi (seconds[1]);

  if (fmaj < smaj) {
    ret = -1;
    goto beach;
  }
  if (fmaj > smaj) {
    ret = 1;
    goto beach;
  }

  /* fmaj == smaj */
  if (fmin < smin) {
    ret = -1;
    goto beach;
  }
  if (fmin > smin) {
    ret = 1;
    goto beach;
  }
  ret = 0;

beach:
  g_strfreev (firsts);
  g_strfreev (seconds);
  return ret;
}

static void
find_highest_version (gchar * key, gchar * value, gchar ** highest)
{
  if (*highest == NULL) {
    /* first value, so just set it */
    *highest = key;
  }
  if (compare_major_minor (key, *highest) > 0)
    *highest = key;
}

/* Libtool creates shell scripts named "base" that calls actual binaries as
 * .libs/lt-base.  If we detect this is a libtool script, unmangle so we
 * find the right binaries */
static void
unmangle_libtool (gchar ** dir, gchar ** base)
{
  gchar *new_dir, *new_base;

  if (!*dir)
    return;
  if (!*base)
    return;

  /* We assume libtool when base starts with "lt-" and dir ends with ".libs".
   * On Windows libtool doesn't seem to be adding "lt-" prefix. */
#ifndef G_OS_WIN32
  if (!g_str_has_prefix (*base, "lt-"))
    return;
#endif

  if (!g_str_has_suffix (*dir, ".libs"))
    return;

#ifndef G_OS_WIN32
  new_base = g_strdup (&((*base)[3]));
#else
  new_base = g_strdup (*base);
#endif
  new_dir = g_path_get_dirname (*dir);
  g_free (*base);
  g_free (*dir);
  *base = new_base;
  *dir = new_dir;
}

/* Returns a directory path that contains the binary given as an argument.
 * If the binary given contains a path, it gets looked for in that path.
 * If it doesn't contain a path, it gets looked for in the standard path.
 *
 * The returned string is newly allocated.
 */
static gchar *
get_dir_of_binary (const gchar * binary)
{
  gchar *base, *dir;
  gchar *full;

  base = g_path_get_basename (binary);
  dir = g_path_get_dirname (binary);

  /* if putting these two together yields the same as binary,
   * then we have the right breakup.  If not, it's because no path was
   * specified which caused get_basename to return "." */
  full = g_build_filename (dir, base, NULL);

#ifdef G_OS_WIN32

  /* g_build_filename() should be using the last path separator used in the
   * input according to the docs, but doesn't actually do that, so we have
   * to fix up the result. */
  {
    gchar *tmp;

    for (tmp = (gchar *) binary + strlen (binary) - 1; tmp >= binary; tmp--) {
      if (*tmp == '/' || *tmp == '\\') {
        full[strlen (dir)] = *tmp;
        break;
      }
    }
  }
#endif

  if (strcmp (full, binary) != 0) {
    if (strcmp (dir, ".") != 0) {
      g_warning ("This should not happen, g_path_get_dirname () has changed.");
      g_free (base);
      g_free (dir);
      g_free (full);
      return NULL;
    }

    /* we know no path was specified, so search standard path for binary */
    g_free (full);
    full = g_find_program_in_path (base);
    if (!full) {
      g_warning ("This should not happen, %s not in standard path.", base);
      g_free (base);
      g_free (dir);
      return NULL;
    }
  }

  g_free (base);
  g_free (dir);
  dir = g_path_get_dirname (full);
  g_free (full);

  return dir;
}

/* Search the given directory for candidate binaries matching the base binary.
 * Return a GHashTable of major/minor -> directory pairs
 */
static GHashTable *
get_candidates (const gchar * dir, const gchar * base)
{
  GDir *gdir;
  GError *error = NULL;
  const gchar *entry;
  gchar *path;
  gchar *suffix, *copy;

  gchar *pattern;
  GPatternSpec *spec, *specexe;

  gchar **dirs;
  gchar **cur;

  GHashTable *candidates = NULL;

  candidates = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  /* compile our pattern specs */
  pattern = g_strdup_printf ("%s-*.*", base);
  spec = g_pattern_spec_new (pattern);
  g_free (pattern);
  pattern = g_strdup_printf ("%s-*.*.exe", base);
  specexe = g_pattern_spec_new (pattern);
  g_free (pattern);

  /* get all dirs from the path and prepend with given dir */
  if (dir)
    path = g_strdup_printf ("%s%c%s",
        dir, G_SEARCHPATH_SEPARATOR, g_getenv ("PATH"));
  else
    path = (gchar *) g_getenv ("PATH");
  dirs = g_strsplit (path, G_SEARCHPATH_SEPARATOR_S, 0);
  if (dir)
    g_free (path);

  /* check all of these in reverse order by winding to bottom and going up  */
  cur = &dirs[0];
  while (*cur)
    ++cur;

  while (cur != &dirs[0]) {
    --cur;
    if (!g_file_test (*cur, G_FILE_TEST_EXISTS) ||
        !g_file_test (*cur, G_FILE_TEST_IS_DIR)) {
      continue;
    }

    gdir = g_dir_open (*cur, 0, &error);
    if (!gdir) {
      g_warning ("Could not open dir %s: %s", *cur, error->message);
      g_error_free (error);
      return NULL;
    }
    while ((entry = g_dir_read_name (gdir))) {
      if (g_pattern_match_string (spec, entry)
          || g_pattern_match_string (specexe, entry)) {
        gchar *full;

        /* is it executable ? */
        full = g_build_filename (*cur, entry, NULL);
        if (!g_file_test (full, G_FILE_TEST_IS_EXECUTABLE)) {
          g_free (full);
          continue;
        }
        g_free (full);

        /* strip base and dash from it */
        suffix = g_strdup (&(entry[strlen (base) + 1]));
        copy = g_strdup (suffix);

        /* strip possible .exe from copy */
        if (g_strrstr (copy, ".exe"))
          g_strrstr (copy, ".exe")[0] = '\0';

        /* stricter pattern check: check if it only contains digits or dots */
        g_strcanon (copy, "0123456789.", 'X');
        if (strstr (copy, "X")) {
          g_free (suffix);
          g_free (copy);
          continue;
        }
        g_free (copy);
        g_hash_table_insert (candidates, suffix, g_strdup (*cur));
      }
    }
  }

  g_strfreev (dirs);
  g_pattern_spec_free (spec);
  return candidates;
}

int
main (int argc, char **argv)
{
  GHashTable *candidates;
  gchar *dir;
  gchar *base;
  gchar *highest = NULL;
  gchar *binary;                /* actual binary we're going to run */
  gchar *path = NULL;           /* and its path */
  gchar *desc;
  GOptionContext *ctx;
  GError *err = NULL;

  /* detect stuff */
  dir = get_dir_of_binary (argv[0]);
  base = g_path_get_basename (argv[0]);

  /* parse command line options */
  desc = g_strdup_printf ("wrapper to call versioned %s", base);
  ctx = g_option_context_new (desc);
  g_free (desc);
  g_option_context_set_ignore_unknown_options (ctx, TRUE);
  g_option_context_add_main_entries (ctx, wrapper_options, GETTEXT_PACKAGE);
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }
  g_option_context_free (ctx);

  /* unmangle libtool if necessary */
  unmangle_libtool (&dir, &base);

#ifdef G_OS_WIN32
  /* remove .exe suffix, otherwise we'll be looking for gst-blah.exe-*.* */
  if (strlen (base) > 4 && g_str_has_suffix (base, ".exe")) {
    base[strlen (base) - 4] = '\0';
  }
#endif

  /* get all candidate binaries */
  candidates = get_candidates (dir, base);
  g_free (dir);

  if (_arg_mm) {
    /* if a version was forced, look it up in the hash table */
    dir = g_hash_table_lookup (candidates, _arg_mm);
    if (!dir) {
      g_print ("ERROR: Major/minor %s of tool %s not found.\n", _arg_mm, base);
      return 1;
    }
    binary = g_strdup_printf ("%s-%s", base, _arg_mm);
  } else {
    highest = NULL;

    /* otherwise, just look up the highest version */
    if (candidates) {
      g_hash_table_foreach (candidates, (GHFunc) find_highest_version,
          &highest);
    }

    if (highest == NULL) {
      g_print ("ERROR: No version of tool %s found.\n", base);
      return 1;
    }
    dir = g_hash_table_lookup (candidates, highest);
    binary = g_strdup_printf ("%s-%s", base, highest);
  }

  g_free (base);

  path = g_build_filename (dir, binary, NULL);
  g_free (binary);

  /* print out list of major/minors we found if asked for */
  /* FIXME: do them in order by creating a GList of keys and sort them */
  if (_arg_list_mm) {
    g_hash_table_foreach (candidates, (GHFunc) hash_print_key, NULL);
    g_hash_table_destroy (candidates);
    return 0;
  }

  /* print out command line if asked for */
  argv[0] = path;
  if (_print) {
    int i;

    for (i = 0; i < argc; ++i) {
      g_print ("%s", argv[i]);
      if (i < argc - 1)
        g_print (" ");
    }
    g_print ("\n");
  }

  /* execute */
  if (execv (path, argv) == -1) {
    g_warning ("Error executing %s: %s (%d)", path, g_strerror (errno), errno);
  }
  g_free (path);

  return 0;
}
