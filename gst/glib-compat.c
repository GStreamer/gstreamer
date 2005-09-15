/*
 * glib-compat.c
 * Functions copied from glib 2.6 and 2.8
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

/* gfileutils.c - File utility functions
 *
 *  Copyright 2000 Red Hat, Inc.
 *
 * GLib is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * GLib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GLib; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *   Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>

#if 0
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef G_OS_WIN32
#include <sys/wait.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#endif


#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#endif /* G_OS_WIN32 */


#ifdef G_OS_WIN32
#define G_DIR_SEPARATOR '\\'
#define G_DIR_SEPARATOR_S "\\"
#define G_IS_DIR_SEPARATOR(c) ((c) == G_DIR_SEPARATOR || (c) == '/')
#define G_SEARCHPATH_SEPARATOR ';'
#define G_SEARCHPATH_SEPARATOR_S ";"
#else
#define G_DIR_SEPARATOR '/'
#define G_DIR_SEPARATOR_S "/"
#define G_IS_DIR_SEPARATOR(c) ((c) == G_DIR_SEPARATOR)
#define G_SEARCHPATH_SEPARATOR ':'
#define G_SEARCHPATH_SEPARATOR_S ":"
#endif


#if !GLIB_CHECK_VERSION (2, 8, 0)
/**
 * g_mkdir_with_parents:
 * @pathname: a pathname in the GLib file name encoding
 * @mode: permissions to use for newly created directories
 *
 * Create a directory if it doesn't already exist. Create intermediate
 * parent directories as needed, too.
 *
 * Returns: 0 if the directory already exists, or was successfully
 * created. Returns -1 if an error occurred, with errno set.
 *
 * Since: 2.8
 */
int
g_mkdir_with_parents (const gchar * pathname, int mode)
{
  gchar *fn, *p;

  if (pathname == NULL || *pathname == '\0') {
    errno = EINVAL;
    return -1;
  }

  fn = g_strdup (pathname);

  if (g_path_is_absolute (fn))
    p = (gchar *) g_path_skip_root (fn);
  else
    p = fn;

  do {
    while (*p && !G_IS_DIR_SEPARATOR (*p))
      p++;

    if (!*p)
      p = NULL;
    else
      *p = '\0';

    if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
      if (g_mkdir (fn, mode) == -1) {
        int errno_save = errno;

        g_free (fn);
        errno = errno_save;
        return -1;
      }
    } else if (!g_file_test (fn, G_FILE_TEST_IS_DIR)) {
      g_free (fn);
      errno = ENOTDIR;
      return -1;
    }
    if (p) {
      *p++ = G_DIR_SEPARATOR;
      while (*p && G_IS_DIR_SEPARATOR (*p))
        p++;
    }
  }
  while (p);

  g_free (fn);

  return 0;
}
#endif


#if !GLIB_CHECK_VERSION (2, 6, 0)
/**
 * g_mkdir: 
 * @filename: a pathname in the GLib file name encoding (UTF-8 on Windows)
 * @mode: permissions to use for the newly created directory
 *
 * A wrapper for the POSIX mkdir() function. The mkdir() function 
 * attempts to create a directory with the given name and permissions.
 * 
 * See the C library manual for more details about mkdir().
 *
 * Returns: 0 if the directory was successfully created, -1 if an error 
 *    occurred
 * 
 * Since: 2.6
 */
int
g_mkdir (const gchar * filename, int mode)
{
#ifdef G_OS_WIN32
  if (G_WIN32_HAVE_WIDECHAR_API ()) {
    wchar_t *wfilename = g_utf8_to_utf16 (filename, -1, NULL, NULL, NULL);
    int retval;
    int save_errno;

    if (wfilename == NULL) {
      errno = EINVAL;
      return -1;
    }

    retval = _wmkdir (wfilename);
    save_errno = errno;

    g_free (wfilename);

    errno = save_errno;
    return retval;
  } else {
    gchar *cp_filename = g_locale_from_utf8 (filename, -1, NULL, NULL, NULL);
    int retval;
    int save_errno;

    if (cp_filename == NULL) {
      errno = EINVAL;
      return -1;
    }

    retval = mkdir (cp_filename);
    save_errno = errno;

    g_free (cp_filename);

    errno = save_errno;
    return retval;
  }
#else
  return mkdir (filename, mode);
#endif
}
#endif

#if !GLIB_CHECK_VERSION (2, 6, 0)
/**
 * g_stat: 
 * @filename: a pathname in the GLib file name encoding (UTF-8 on Windows)
 * @buf: a pointer to a <structname>stat</structname> struct, which
 *    will be filled with the file information
 *
 * A wrapper for the POSIX stat() function. The stat() function 
 * returns information about a file.
 * 
 * See the C library manual for more details about stat().
 *
 * Returns: 0 if the information was successfully retrieved, -1 if an error 
 *    occurred
 * 
 * Since: 2.6
 */
int
g_stat (const gchar * filename, struct stat *buf)
{
#ifdef G_OS_WIN32
  if (G_WIN32_HAVE_WIDECHAR_API ()) {
    wchar_t *wfilename = g_utf8_to_utf16 (filename, -1, NULL, NULL, NULL);
    int retval;
    int save_errno;

    if (wfilename == NULL) {
      errno = EINVAL;
      return -1;
    }

    retval = _wstat (wfilename, (struct _stat *) buf);
    save_errno = errno;

    g_free (wfilename);

    errno = save_errno;
    return retval;
  } else {
    gchar *cp_filename = g_locale_from_utf8 (filename, -1, NULL, NULL, NULL);
    int retval;
    int save_errno;

    if (cp_filename == NULL) {
      errno = EINVAL;
      return -1;
    }

    retval = stat (cp_filename, buf);
    save_errno = errno;

    g_free (cp_filename);

    errno = save_errno;
    return retval;
  }
#else
  return stat (filename, buf);
#endif
}
#endif
