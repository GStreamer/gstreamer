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
#  include "config.h"
#endif

/***** until we have gettext set up properly, don't even try this*/

# ifdef ENABLE_NLS
#  include <libintl.h>
#  define _(String) dgettext(GETTEXT_PACKAGE,String)
#  ifdef gettext_noop
#   define N_(String) gettext_noop(String)
#  else /* gettext_noop */
#   define N_(String) (String)
#  endif /* gettext_noop */
# else /* ENABLE_NLS */
#  define _(String) (String)
#  define N_(String) (String)
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,String) (String)
#  define dcgettext(Domain,String,Type) (String)
#  define bindtextdomain(Domain,Directory) (Domain)
# endif /* ENABLE_NLS */

#include <stdlib.h>
#include <string.h>

/*** debugging categories *****************************************************/

#ifndef GST_DISABLE_GST_DEBUG

#include <gst/gstinfo.h>

extern GstDebugCategory *GST_CAT_GST_INIT;
extern GstDebugCategory *GST_CAT_COTHREADS;
extern GstDebugCategory *GST_CAT_COTHREAD_SWITCH;
extern GstDebugCategory *GST_CAT_AUTOPLUG;
extern GstDebugCategory *GST_CAT_AUTOPLUG_ATTEMPT;
extern GstDebugCategory *GST_CAT_PARENTAGE;
extern GstDebugCategory *GST_CAT_STATES;
extern GstDebugCategory *GST_CAT_PLANNING;
extern GstDebugCategory *GST_CAT_SCHEDULING;
extern GstDebugCategory *GST_CAT_DATAFLOW;
extern GstDebugCategory *GST_CAT_BUFFER;
extern GstDebugCategory *GST_CAT_CAPS;
extern GstDebugCategory *GST_CAT_CLOCK;
extern GstDebugCategory *GST_CAT_ELEMENT_PADS;
extern GstDebugCategory *GST_CAT_ELEMENT_FACTORY;
extern GstDebugCategory *GST_CAT_PADS;
extern GstDebugCategory *GST_CAT_PIPELINE;
extern GstDebugCategory *GST_CAT_PLUGIN_LOADING;
extern GstDebugCategory *GST_CAT_PLUGIN_INFO;
extern GstDebugCategory *GST_CAT_PROPERTIES;
extern GstDebugCategory *GST_CAT_THREAD;
extern GstDebugCategory *GST_CAT_XML;
extern GstDebugCategory *GST_CAT_NEGOTIATION;
extern GstDebugCategory *GST_CAT_REFCOUNTING;
extern GstDebugCategory *GST_CAT_EVENT;
extern GstDebugCategory *GST_CAT_PARAMS;
extern GstDebugCategory *GST_CAT_CALL_TRACE;

#endif

#endif /* __GST_PRIVATE_H__ */
