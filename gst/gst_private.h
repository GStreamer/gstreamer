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

#include <glib.h>

#include <stdlib.h>
#include <string.h>

extern const char             *g_log_domain_gstreamer;

gboolean __gst_in_valgrind (void);

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
extern GstDebugCategory *GST_CAT_PADS;
extern GstDebugCategory *GST_CAT_PIPELINE;
extern GstDebugCategory *GST_CAT_PLUGIN_LOADING;
extern GstDebugCategory *GST_CAT_PLUGIN_INFO;
extern GstDebugCategory *GST_CAT_PROPERTIES;
extern GstDebugCategory *GST_CAT_THREAD;
extern GstDebugCategory *GST_CAT_XML;
extern GstDebugCategory *GST_CAT_NEGOTIATION;
extern GstDebugCategory *GST_CAT_REFCOUNTING;
extern GstDebugCategory *GST_CAT_ERROR_SYSTEM;
extern GstDebugCategory *GST_CAT_EVENT;
extern GstDebugCategory *GST_CAT_PARAMS;
extern GstDebugCategory *GST_CAT_CALL_TRACE;

#endif

#endif /* __GST_PRIVATE_H__ */
