
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <stdio.h>
#include <debug.h>

static const char *resample_debug_level_names[] = {
  "NONE",
  "ERROR",
  "WARNING",
  "INFO",
  "DEBUG",
  "LOG"
};

static int resample_debug_level = RESAMPLE_LEVEL_ERROR;

void
resample_debug_log (int level, const char *file, const char *function,
    int line, const char *format, ...)
{
#ifndef GLIB_COMPAT
  va_list varargs;
  char *s;

  if (level > resample_debug_level)
    return;

  va_start (varargs, format);
  s = g_strdup_vprintf (format, varargs);
  va_end (varargs);

  fprintf (stderr, "RESAMPLE: %s: %s(%d): %s: %s\n",
      resample_debug_level_names[level], file, line, function, s);
  g_free (s);
#else
  va_list varargs;
  char s[1000];

  if (level > resample_debug_level)
    return;

  va_start (varargs, format);
  vsnprintf (s, 999, format, varargs);
  va_end (varargs);

  fprintf (stderr, "RESAMPLE: %s: %s(%d): %s: %s\n",
      resample_debug_level_names[level], file, line, function, s);
#endif
}

void
resample_debug_set_level (int level)
{
  resample_debug_level = level;
}

int
resample_debug_get_level (void)
{
  return resample_debug_level;
}
