/*
 * glib-compat.c
 * Functions copied from glib 2.10
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

#include "gst_private.h" /* for g_warning */
#include <glib.h>

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION(2,25,0)
typedef struct stat GStatBuf;
#endif

/* copies */

/* adaptations */

G_END_DECLS
