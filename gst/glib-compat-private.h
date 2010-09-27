/*
 * glib-compat.c
 * Functions copied from glib 2.10
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

#ifndef __GLIB_COMPAT_PRIVATE_H__
#define __GLIB_COMPAT_PRIVATE_H__

#include <glib.h>

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION(2,25,0)
typedef struct stat GStatBuf;
#endif

#if GLIB_CHECK_VERSION(2,26,0)
#define GLIB_HAS_GDATETIME
#endif

/* copies */

/* adaptations */

G_END_DECLS

#endif
