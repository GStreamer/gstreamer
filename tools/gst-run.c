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
#include <unistd.h>
#include <errno.h>
#include <popt.h>
#include <glib.h>

enum
{
  ARG_MM = 1,
  ARG_LIST_MM,
  ARG_PRINT,
  ARG_HELP
};

/* global statics for option parsing */
static gboolean _print = FALSE;
static gchar * _arg_mm = NULL;
static gboolean _arg_list_mm = FALSE;

/* callback to parse arguments */
static void
popt_callback (poptContext context, enum poptCallbackReason reason,
               const struct poptOption *option, const char *arg, void *data)
{
  if (reason == POPT_CALLBACK_REASON_OPTION)
  {
    switch (option->val)
    {
      case ARG_MM:
        _arg_mm = g_strdup (arg);
        break;
      case ARG_LIST_MM:
        _arg_list_mm = TRUE;
        break;
      case ARG_PRINT:
        _print = TRUE;
        break;
      case ARG_HELP:
        poptPrintHelp (context, stdout, 0);
        g_print ("\n");
        break;
    }
  }
  else
  {
    g_print ("Unknown reason for callback\n");
  }
}

/* popt options table for the wrapper */
static struct poptOption wrapper_options[] =
{
  { NULL, '\0',
    POPT_ARG_CALLBACK,
    (void *) &popt_callback, 0, NULL, NULL },
  { "help", '\0',
    POPT_ARG_NONE | POPT_ARGFLAG_DOC_HIDDEN,
    NULL, ARG_HELP, ("Show help"), NULL },
  { "?", '\0',
    POPT_ARG_NONE | POPT_ARGFLAG_STRIP | POPT_ARGFLAG_ONEDASH
                  | POPT_ARGFLAG_DOC_HIDDEN,
    NULL, ARG_HELP, NULL, NULL },
  /* We cheat by specifying -p as long "p" with onedash, so that it
     also gets stripped properly from our arg flags */
  { "p", '\0',
    POPT_ARG_NONE | POPT_ARGFLAG_STRIP | POPT_ARGFLAG_ONEDASH
                  | POPT_ARGFLAG_DOC_HIDDEN,
    NULL, ARG_PRINT, NULL, NULL },
  { "print", '\0',
    POPT_ARG_NONE | POPT_ARGFLAG_STRIP,
    NULL, ARG_PRINT, ("Print wrapped command line"), NULL },
  { "gst-mm", '\0',
    POPT_ARG_STRING | POPT_ARGFLAG_STRIP,
    NULL, ARG_MM, ("Force major/minor version"), NULL },
  { "gst-list-mm", '\0',
    POPT_ARG_NONE | POPT_ARGFLAG_STRIP,
    NULL, ARG_LIST_MM, ("List found major/minor versions"), NULL },
  POPT_TABLEEND
};

/* helper table including our wrapper options */
static struct poptOption options[] =
{
  { NULL, '\0', POPT_ARG_INCLUDE_TABLE, wrapper_options,  0,
    "Wrapper options:", NULL},
  POPT_TABLEEND
};


/* print out the major/minor, which is the hash key */
static void
hash_print_key (gchar * key, gchar * value)
{
  g_print ("%s\n", (gchar *) key);
}

static void
find_highest_version (gchar * key, gchar * value, gchar ** highest)
{
  if (*highest == NULL)
  {
    /* first value, so just set it */
    *highest = key;
  }
  if (strcmp (key, *highest) > 0) *highest = key;
}

/* Libtool creates shell scripts named "base" that calls actual binaries as
 * .libs/lt-base.  If we detect this is a libtool script, unmangle so we
 * find the right binaries */
static void
unmangle_libtool (gchar ** dir, gchar ** base)
{
  gchar *new_dir, *new_base;

  if (!*dir) return;
  if (!*base) return;

  /* we assume libtool when base starts with lt- and dir ends with .libs */
  if (!g_str_has_prefix (*base, "lt-")) return;
  if (!g_str_has_suffix (*dir, ".libs")) return;

  new_base = g_strdup (&((*base)[3]));
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
gchar *
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

  if (strcmp (full, binary) != 0)
  {
    if (strcmp (dir, ".") != 0)
    {
      g_warning ("This should not happen, g_path_get_dirname () has changed.");
      g_free (base);
      g_free (dir);
      g_free (full);
      return NULL;
    }

    /* we know no path was specified, so search standard path for binary */
    g_free (full);
    full = g_find_program_in_path (base);
    if (!full)
    {
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
GHashTable *
get_candidates (const gchar * dir, const gchar * base)
{
  GDir *gdir;
  GError *error = NULL;
  const gchar *entry;
  gchar *path;
  gchar *suffix;

  gchar *pattern;
  GPatternSpec *spec;


  gchar *test;

  gchar **dirs;
  gchar **cur;

  GHashTable *candidates = NULL;

  candidates = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  /* compile our pattern spec */
  pattern = g_strdup_printf ("%s-*.*", base);
  spec = g_pattern_spec_new (pattern);
  g_free (pattern);

  /* get all dirs from the path and prepend with given dir */
  path = g_strdup_printf ("%s%c%s",
                          dir, G_SEARCHPATH_SEPARATOR, getenv ("PATH"));
  dirs = g_strsplit (path, G_SEARCHPATH_SEPARATOR_S, 0);
  g_free (path);

  /* check all of these in reverse order by winding to bottom and going up  */
  cur = &dirs[0];
  while (*cur) ++cur;

  while (cur != &dirs[0])
  {
    --cur;
    if (! g_file_test (*cur, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
      continue;

    gdir = g_dir_open (*cur, 0, &error);
    if (! gdir)
    {
      g_warning ("Could not open dir %s: %s", *cur, error->message);
      g_error_free (error);
      return NULL;
    }
    while ((entry = g_dir_read_name (gdir)))
    {
      if (g_pattern_match_string (spec, entry))
      {
        gchar *full;

        /* is it executable ? */
        full = g_build_filename (*cur, entry, NULL);
        if (! g_file_test (full, G_FILE_TEST_IS_EXECUTABLE))
        {
          g_free (full);
          continue;
        }
        g_free (full);

        /* strip base and dash from it */
        suffix = g_strdup (&(entry[strlen (base) + 1]));

        /* stricter pattern check: check if it only contains digits or dots */
        test = g_strdup (suffix);
        g_strcanon (test, "0123456789.", 'X');
        if (strstr (test, "X"))
        {
          g_free (test);
          continue;
        }
        g_free (test);
        g_hash_table_insert (candidates, suffix, g_strdup (*cur));
      }
    }
  }

  g_strfreev (dirs);
  g_pattern_spec_free (spec);
  return candidates;
}

int main
(int argc, char **argv)
{
  GHashTable *candidates;
  gchar *dir;
  gchar *base;
  gchar *highest = NULL;
  gchar *binary;		/* actual binary we're going to run */
  gchar *path = NULL;		/* and its path */
  poptContext ctx;
  int nextopt;

  /* parse command line options */
  ctx = poptGetContext ("gst-run", argc, (const char **) argv, options, 0);
  poptReadDefaultConfig (ctx, TRUE);
  while ((nextopt = poptGetNextOpt (ctx)) > 0)
    ;

  argc = poptStrippedArgv (ctx, argc, argv);
  argv[argc] = NULL;
  poptFreeContext (ctx);

    /* detect stuff */
  dir = get_dir_of_binary (argv[0]);
  base = g_path_get_basename (argv[0]);

  /* unmangle libtool if necessary */
  unmangle_libtool (&dir, &base);

  /* get all candidate binaries */
  candidates = get_candidates (dir, base);
  g_free (dir);

  if (_arg_mm)
  {
    /* if a version was forced, look it up in the hash table */
    dir = g_hash_table_lookup (candidates, _arg_mm);
    if (!dir)
    {
      g_print ("ERROR: Major/minor %s of tool %s not found.\n", _arg_mm, base);
      return 1;
    }
    binary = g_strdup_printf ("%s-%s", base, _arg_mm);
  }
  else
  {
    /* otherwise, just look up the highest version */
    g_hash_table_foreach (candidates, (GHFunc) find_highest_version,
                          &highest);
    dir = g_hash_table_lookup (candidates, highest);
    if (!dir)
    {
      g_print ("ERROR: No version of tool %s not found.\n", base);
      return 1;
    }
    binary = g_strdup_printf ("%s-%s", base, highest);
  }

  path = g_build_filename (dir, binary, NULL);
  g_free (binary);

  /* print out list of major/minors we found if asked for */
  /* FIXME: do them in order by creating a GList of keys and sort them */
  if (_arg_list_mm)
  {
    g_hash_table_foreach (candidates, (GHFunc) hash_print_key, NULL);
    return 0;
  }

  /* print out command line if asked for */
  argv[0] = path;
  if (_print)
  {
    int i;
    for (i = 0; i < argc; ++i)
    {
      g_print ("%s", argv[i]);
      if (i < argc - 1) g_print (" ");
    }
    g_print ("\n");
  }

  /* execute */
  if (execv (path, argv) == -1)
  {
    g_warning ("Error executing %s: %s (%d)", path, g_strerror (errno), errno);
  }
  g_free (path);

  return 0;
}
