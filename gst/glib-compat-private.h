/*
 * glib-compat.c
 * Functions copied from glib 2.10
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

#include "gst_private.h" /* for g_warning */
#include <glib.h>

G_BEGIN_DECLS

/* copies */

/* adaptations */

/* FIXME: remove once we depend on GLib 2.10 */
#if (!GLIB_CHECK_VERSION (2, 10, 0))
#define g_intern_string(s) g_quark_to_string(g_quark_from_string(s))
#endif

G_END_DECLS
