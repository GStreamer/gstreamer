/*
 * glib-compat.c
 * Functions copied from glib 2.6 and 2.8
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
 */

#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <stdio.h>
#include <errno.h>

#include "glib-compat.h"
#include "glib-compat-private.h"

#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/types.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#endif /* G_OS_WIN32 */
