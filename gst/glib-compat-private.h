/*
 * glib-compat.c
 * Functions copied from glib 2.8
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

#include "gst_private.h" /* for g_warning */
#include <glib.h>

G_BEGIN_DECLS

/* copies */
#if !GLIB_CHECK_VERSION (2, 8, 0)
int g_mkdir_with_parents (const gchar *pathname, int          mode);
#endif

/* adaptations */
#include <glib-object.h>
GFlagsValue*
gst_flags_get_first_value (GFlagsClass *flags_class,
                           guint        value);

GObject*
g_value_dup_gst_object (const GValue *value);
G_END_DECLS

