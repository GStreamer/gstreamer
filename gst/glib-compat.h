/*
 * glib-compat.c
 * Functions copied from glib 2.6 and 2.8
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

#include "gst_private.h" /* for g_warning */
#include <glib.h>
#if GLIB_CHECK_VERSION (2, 6, 0)
#include <glib/gstdio.h>
#endif

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION (2, 8, 0)
int g_mkdir_with_parents (const gchar *pathname, int          mode);
#endif

#if !GLIB_CHECK_VERSION (2, 6, 0)
int g_mkdir (const gchar *filename, int          mode);
#endif

#if !GLIB_CHECK_VERSION (2, 6, 0)
struct stat;

int g_stat (const gchar *filename, struct stat *buf);
#endif

#include <glib-object.h>
GFlagsValue*
gst_flags_get_first_value (GFlagsClass *flags_class,
                           guint        value);
G_END_DECLS

