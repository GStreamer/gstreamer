/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst_private.h: Private header for within libgst
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


#ifndef __GST_PRIVATE_H__
#define __GST_PRIVATE_H__

#ifdef HAVE_CONFIG_H
#include "config.h"

/***** until we have gettext set up properly, don't even try this
#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) dgettext(PACKAGE,String)
#ifdef gettext_noop
#define N_(String) gettext_noop(String)
#else /* gettext_noop */
#define N_(String) (String)
#endif /* gettext_noop */
#else /* ENABLE_NLS */
#define _(String) (String)
#define N_(String) (String)
#define textdomain(String) (String)
#define gettext(String) (String)
#define dgettext(Domain,String) (String)
#define dcgettext(Domain,String,Type) (String)
#define bindtextdomain(Domain,Directory) (Domain)
#endif /* ENABLE_NLS */
*****/

#endif /* HAVE_CONFIG_H */


#include <stdlib.h>
#include <string.h>

#include <gst/gstinfo.h>

#endif /* __GST_PRIVATE_H__ */
