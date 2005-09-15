/*
 * glib-compat.c
 * Functions copied from glib 2.6 and 2.8
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

#include <glib.h>

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION (2, 8, 0)
int g_mkdir_with_parents (const gchar *pathname, int          mode);
#endif

#if !GLIB_CHECK_VERSION (2, 6, 0)
int g_mkdir (const gchar *filename, int          mode);
#endif

#if !GLIB_CHECK_VERSION (2, 6, 0)
int g_stat (const gchar *filename, struct stat *buf);
#endif

G_END_DECLS

